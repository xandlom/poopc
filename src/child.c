#include "child.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h> // siginfo_t; sys/wait.h alone does not declare it on FreeBSD
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "poop.h"

#define STDERR_CAP 4096

// Both ends close-on-exec. pipe2 is not portable to Darwin, and poop is single
// threaded, so there is no fork to race the two fcntls against.
static int pipe_cloexec(int p[2]) {
    if (pipe(p) != 0) return -1;
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(p[i], F_GETFD);
        if (flags < 0 || fcntl(p[i], F_SETFD, flags | FD_CLOEXEC) < 0) {
            int e = errno;
            close(p[0]);
            close(p[1]);
            errno = e;
            return -1;
        }
    }
    return 0;
}

static void close_pair(int p[2]) {
    close(p[0]);
    close(p[1]);
}

// Reads the child's stderr to EOF, keeping the first STDERR_CAP bytes.
static unsigned char *drain_stderr(int fd, size_t *out_len, bool *out_truncated) {
    unsigned char *cap = malloc(STDERR_CAP + 1);
    if (!cap) return NULL;
    size_t len = 0;
    bool truncated = false;
    for (;;) {
        unsigned char tmp[4096];
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        size_t room = (len < STDERR_CAP) ? (size_t)(STDERR_CAP - len) : 0;
        size_t take = ((size_t)n < room) ? (size_t)n : room;
        if (take > 0) {
            memcpy(cap + len, tmp, take);
            len += take;
        }
        if ((size_t)n > take) truncated = true;
    }
    cap[len] = '\0';
    *out_len = len;
    *out_truncated = truncated;
    return cap;
}

int child_run(char *const argv[], Counters *c, ChildResult *out) {
    memset(out, 0, sizeof(*out));

    // Closing the parent's end releases the child from its pre-exec wait.
    int gate[2];
    if (pipe_cloexec(gate) != 0) return -1;

    // The child writes its exec errno here (then _exit) if execvp fails; the
    // pipe is close-on-exec, so a successful exec leaves the read end at EOF.
    int exec_pipe[2];
    if (pipe_cloexec(exec_pipe) != 0) {
        int e = errno;
        close_pair(gate);
        errno = e;
        return -1;
    }

    int err_pipe[2]; // child stderr
    if (pipe_cloexec(err_pipe) != 0) {
        int e = errno;
        close_pair(gate);
        close_pair(exec_pipe);
        errno = e;
        return -1;
    }

    counters_prepare(c);

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close_pair(gate);
        close_pair(exec_pipe);
        close_pair(err_pipe);
        errno = e;
        return -1;
    }

    if (pid == 0) {
        close(gate[1]);
        close(exec_pipe[0]);
        close(err_pipe[0]);

        // Wait for the go-ahead, so backends that address counters by pid have
        // attached before any of the command's own instructions retire.
        char ignored;
        while (read(gate[0], &ignored, 1) < 0 && errno == EINTR) {}
        close(gate[0]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            if (devnull != STDOUT_FILENO) close(devnull);
        }
        dup2(err_pipe[1], STDERR_FILENO);
        if (err_pipe[1] != STDERR_FILENO) close(err_pipe[1]);

        execvp(argv[0], argv);

        int e = errno;
        ssize_t w = write(exec_pipe[1], &e, sizeof(e));
        (void)w;
        _exit(127);
    }

    close(gate[0]);
    close(exec_pipe[1]);
    close(err_pipe[1]);

    counters_child_spawned(c, pid);

    uint64_t start = now_ns();
    close(gate[1]); // the child execs now

    int child_errno = 0;
    ssize_t got = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    close(exec_pipe[0]);
    if (got == (ssize_t)sizeof(child_errno)) {
        int status;
        waitpid(pid, &status, 0);
        close(err_pipe[0]);
        (void)counters_after_reap(c);
        errno = child_errno;
        return -1;
    }

    size_t caplen = 0;
    bool truncated = false;
    unsigned char *cap = drain_stderr(err_pipe[0], &caplen, &truncated);
    close(err_pipe[0]);
    if (!cap) {
        int status;
        waitpid(pid, &status, 0);
        (void)counters_after_reap(c);
        errno = ENOMEM;
        return -1;
    }

    // Observe the exit without collecting the zombie, so counters addressed by
    // pid are still readable; wait4 below still finds a child to reap.
    siginfo_t si;
    while (waitid(P_PID, (id_t)pid, &si, WEXITED | WNOWAIT) != 0 && errno == EINTR) {}
    counters_before_reap(c);

    int status = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    if (wait4(pid, &status, 0, &ru) < 0) {
        int e = errno;
        free(cap);
        (void)counters_after_reap(c);
        errno = e;
        return -1;
    }
    out->wall_time = now_ns() - start;
    out->counters = counters_after_reap(c);

#if defined(__APPLE__)
    out->peak_rss = (uint64_t)ru.ru_maxrss; // bytes on Darwin
#else
    out->peak_rss = (uint64_t)ru.ru_maxrss * 1024; // KiB on Linux and the BSDs
#endif
    out->stderr_buf = cap;
    out->stderr_len = caplen;
    out->stderr_truncated = truncated;
    if (WIFEXITED(status)) {
        out->exited = true;
        out->exit_code = WEXITSTATUS(status);
    } else {
        out->exited = false;
        out->exit_code = 0;
    }
    return 0;
}

void child_result_free(ChildResult *r) {
    free(r->stderr_buf);
    r->stderr_buf = NULL;
}
