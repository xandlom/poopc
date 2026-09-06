// Linux backend: all five counters via perf_event_open(2).
//
// The counters are opened on poop itself with `inherit`, so the child gets a
// copy at fork, and with `enable_on_exec`, so counting starts when that copy
// execs. Nothing in the group is addressed by pid, which is why a fresh group
// has to be opened for every sample: `enable_on_exec` arms the next exec of the
// task that owns the counter, and poop itself never execs again.
//
// `exclude_kernel` is set, matching upstream, so the counts cover user time
// only. Note that the Darwin backend cannot do the same, so numbers are not
// comparable across platforms.
#include "../counters.h"

#include <errno.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef PERF_FLAG_FD_CLOEXEC
#define PERF_FLAG_FD_CLOEXEC (1UL << 3)
#endif

// Indexed by CounterId; fds[COUNTER_CPU_CYCLES] is the group leader.
static const uint32_t perf_configs[POOP_COUNTER_COUNT] = {
    [COUNTER_CPU_CYCLES] = PERF_COUNT_HW_CPU_CYCLES,
    [COUNTER_INSTRUCTIONS] = PERF_COUNT_HW_INSTRUCTIONS,
    [COUNTER_CACHE_REFERENCES] = PERF_COUNT_HW_CACHE_REFERENCES,
    [COUNTER_CACHE_MISSES] = PERF_COUNT_HW_CACHE_MISSES,
    [COUNTER_BRANCH_MISSES] = PERF_COUNT_HW_BRANCH_MISSES,
};

struct Counters {
    int fds[POOP_COUNTER_COUNT];
    bool open;
    CounterSupport support;
    char reason[128];
};

static long perf_event_open_sys(struct perf_event_attr *attr, pid_t pid, int cpu,
                                int group_fd, unsigned long flags) {
    return syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static void group_close(Counters *c) {
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) {
        if (c->fds[i] >= 0) close(c->fds[i]);
        c->fds[i] = -1;
    }
    c->open = false;
}

// Opens a fresh group armed to start counting on the child's exec. On failure
// every fd is closed again and errno describes the first refusal.
static bool group_open(Counters *c) {
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) c->fds[i] = -1;

    for (int i = 0; i < POOP_COUNTER_COUNT; i++) {
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

        int group_fd = (i == 0) ? -1 : c->fds[0];
        long fd = perf_event_open_sys(&attr, 0, -1, group_fd, PERF_FLAG_FD_CLOEXEC);
        if (fd < 0) {
            int e = errno;
            group_close(c);
            errno = e;
            return false;
        }
        c->fds[i] = (int)fd;
    }
    c->open = true;
    return true;
}

static uint64_t read_fd(int fd) {
    uint64_t v = 0;
    ssize_t n = read(fd, &v, sizeof(v));
    if (n != (ssize_t)sizeof(v)) return 0;
    return v;
}

static void drop_support(Counters *c, int err) {
    memset(&c->support, 0, sizeof(c->support));
    snprintf(c->reason, sizeof(c->reason), "%s", strerror(err));
}

const char *counters_backend_name(void) { return "perf_event_open"; }

Counters *counters_open(void) {
    Counters *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) c->fds[i] = -1;

    // Probe once up front so an unusable kernel is reported before any sampling
    // rather than after the first run.
    if (!group_open(c)) {
        drop_support(c, errno);
        return c;
    }
    group_close(c);
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) c->support.v[i] = true;
    return c;
}

void counters_close(Counters *c) {
    if (!c) return;
    group_close(c);
    free(c);
}

const CounterSupport *counters_support(const Counters *c) { return &c->support; }

bool counters_any_supported(const Counters *c) {
    for (int i = 0; i < POOP_COUNTER_COUNT; i++)
        if (c->support.v[i]) return true;
    return false;
}

const char *counters_unavailable_reason(const Counters *c) {
    return counters_any_supported(c) ? NULL : c->reason;
}

void counters_prepare(Counters *c) {
    if (!counters_any_supported(c)) return;
    if (!group_open(c)) {
        drop_support(c, errno);
        return;
    }
    ioctl(c->fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    ioctl(c->fds[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
}

void counters_child_spawned(Counters *c, pid_t pid) {
    (void)c;
    (void)pid; // the group counts by inheritance, not by pid
}

void counters_before_reap(Counters *c) {
    (void)c; // the fds outlive the child; sampling happens after the reap
}

CounterReadings counters_after_reap(Counters *c) {
    CounterReadings r;
    memset(&r, 0, sizeof(r));
    if (!c->open) return r;

    ioctl(c->fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    for (int i = 0; i < POOP_COUNTER_COUNT; i++)
        if (c->support.v[i]) r.v[i] = read_fd(c->fds[i]);
    group_close(c);
    return r;
}
