#include "perf.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PERF_FLAG_FD_CLOEXEC
#define PERF_FLAG_FD_CLOEXEC (1UL << 3)
#endif

// Same order upstream uses; fds[0] (cpu_cycles) is the group leader.
static const uint32_t perf_configs[POOP_PERF_COUNT] = {
    PERF_COUNT_HW_CPU_CYCLES,       PERF_COUNT_HW_INSTRUCTIONS,
    PERF_COUNT_HW_CACHE_REFERENCES, PERF_COUNT_HW_CACHE_MISSES,
    PERF_COUNT_HW_BRANCH_MISSES,
};

static long perf_event_open_sys(struct perf_event_attr *attr, pid_t pid, int cpu,
                                int group_fd, unsigned long flags) {
    return syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

bool perf_group_open(PerfGroup *g) {
    for (int i = 0; i < POOP_PERF_COUNT; i++) g->fds[i] = -1;

    for (int i = 0; i < POOP_PERF_COUNT; i++) {
        struct perf_event_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.type = PERF_TYPE_HARDWARE;
        attr.size = sizeof(attr);
        attr.config = perf_configs[i];
        attr.disabled = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.inherit = 1;
        attr.enable_on_exec = 1;

        int group_fd = (i == 0) ? -1 : g->fds[0];
        long fd = perf_event_open_sys(&attr, 0, -1, group_fd, PERF_FLAG_FD_CLOEXEC);
        if (fd < 0) {
            int e = errno;
            perf_group_close(g);
            errno = e;
            return false;
        }
        g->fds[i] = (int)fd;
    }
    return true;
}

void perf_group_reset(PerfGroup *g) {
    ioctl(g->fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    ioctl(g->fds[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
}

void perf_group_disable(PerfGroup *g) {
    ioctl(g->fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
}

void perf_group_close(PerfGroup *g) {
    for (int i = 0; i < POOP_PERF_COUNT; i++) {
        if (g->fds[i] >= 0) close(g->fds[i]);
        g->fds[i] = -1;
    }
}

uint64_t perf_read(int fd) {
    uint64_t v = 0;
    ssize_t n = read(fd, &v, sizeof(v));
    if (n != (ssize_t)sizeof(v)) return 0;
    return v;
}

#define STDERR_CAP 4096

static void close_pair(int p[2]) {
    close(p[0]);
    close(p[1]);
}

int spawn_and_wait(char *const argv[], ChildResult *out) {
    memset(out, 0, sizeof(*out));

    // Child writes its exec errno here (then _exit) if execvp fails; the pipe is
    // close-on-exec so a successful exec leaves the read end at EOF.
    int exec_pipe[2];
    if (pipe2(exec_pipe, O_CLOEXEC) != 0) return -1;

    int err_pipe[2]; // child stderr
    if (pipe2(err_pipe, O_CLOEXEC) != 0) {
        int e = errno;
        close_pair(exec_pipe);
        errno = e;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close_pair(exec_pipe);
        close_pair(err_pipe);
        errno = e;
        return -1;
    }

    if (pid == 0) {
        close(exec_pipe[0]);
        close(err_pipe[0]);

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

    close(exec_pipe[1]);
    close(err_pipe[1]);

    int child_errno = 0;
    ssize_t got = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    close(exec_pipe[0]);
    if (got == (ssize_t)sizeof(child_errno)) {
        int st;
        waitpid(pid, &st, 0);
        close(err_pipe[0]);
        errno = child_errno;
        return -1;
    }

    unsigned char *cap = malloc(STDERR_CAP + 1);
    size_t caplen = 0;
    bool truncated = false;
    for (;;) {
        unsigned char tmp[4096];
        ssize_t n = read(err_pipe[0], tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        size_t room = (caplen < STDERR_CAP) ? (size_t)(STDERR_CAP - caplen) : 0;
        size_t take = ((size_t)n < room) ? (size_t)n : room;
        if (take > 0) {
            memcpy(cap + caplen, tmp, take);
            caplen += take;
        }
        if ((size_t)n > take) truncated = true;
    }
    close(err_pipe[0]);
    cap[caplen] = '\0';

    int status = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    if (wait4(pid, &status, 0, &ru) < 0) {
        int e = errno;
        free(cap);
        errno = e;
        return -1;
    }

    out->peak_rss = (uint64_t)ru.ru_maxrss * 1024; // ru_maxrss is KiB on Linux
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
