// Darwin backend: per-process cycle and instruction counts via proc_pid_rusage.
//
// Darwin has no perf_event_open. What it does have is RUSAGE_INFO_V4, whose
// ri_instructions and ri_cycles are PMU-backed and readable for any process the
// caller owns -- no root, no entitlement, no SIP changes.
//
// Two consequences shape this backend:
//
//   * The counters are addressed by pid and are gone once the child is reaped,
//     so they are sampled in counters_before_reap(), which the caller invokes
//     while the child is still a zombie.
//   * There is no way to arm or reset them; they are always-on, whole-lifetime
//     totals for the process. That is exactly what we want here, since every
//     sample is a freshly spawned child. The few instructions that child spends
//     between fork and exec are counted too, which is a constant far below the
//     run-to-run noise.
//
// The remaining three counters poop reports on Linux -- cache references, cache
// misses and branch misses -- are only reachable through the private kperf
// framework, which requires root and counts per-core rather than per-process, so
// they are left unsupported.
//
// Note that unlike the Linux backend, which sets exclude_kernel, these totals
// include kernel time spent on the process's behalf. Numbers are comparable
// between commands measured here, but not against a Linux run.
#include "../counters.h"

#include <errno.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

struct Counters {
    pid_t pid;
    CounterReadings readings;
    CounterSupport support;
    char reason[128];
};

static void drop_support(Counters *c, const char *why) {
    memset(&c->support, 0, sizeof(c->support));
    snprintf(c->reason, sizeof(c->reason), "%s", why);
}

const char *counters_backend_name(void) { return "proc_pid_rusage"; }

Counters *counters_open(void) {
    Counters *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->pid = -1;

    // Probe against poop itself. A machine without a usable PMU still answers
    // the call, it just leaves both fields at zero -- and this process has
    // certainly retired an instruction or two by now.
    struct rusage_info_v4 info;
    memset(&info, 0, sizeof(info));
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&info) != 0) {
        drop_support(c, strerror(errno));
        return c;
    }
    if (info.ri_instructions == 0 && info.ri_cycles == 0) {
        drop_support(c, "no PMU-backed counts in RUSAGE_INFO_V4");
        return c;
    }

    c->support.v[COUNTER_CPU_CYCLES] = true;
    c->support.v[COUNTER_INSTRUCTIONS] = true;
    return c;
}

void counters_close(Counters *c) { free(c); }

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
    c->pid = -1;
    memset(&c->readings, 0, sizeof(c->readings));
}

void counters_child_spawned(Counters *c, pid_t pid) { c->pid = pid; }

void counters_before_reap(Counters *c) {
    if (!counters_any_supported(c) || c->pid < 0) return;

    struct rusage_info_v4 usage;
    memset(&usage, 0, sizeof(usage));
    if (proc_pid_rusage(c->pid, RUSAGE_INFO_V4, (rusage_info_t *)&usage) != 0) {
        drop_support(c, strerror(errno));
        return;
    }

    c->readings.v[COUNTER_CPU_CYCLES] = usage.ri_cycles;
    c->readings.v[COUNTER_INSTRUCTIONS] = usage.ri_instructions;
}

CounterReadings counters_after_reap(Counters *c) { return c->readings; }
