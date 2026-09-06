// FreeBSD backend: hardware counters via hwpmc(4) and libpmc.
//
// Each counter is a process-scope counting PMC (PMC_MODE_TC) allocated per
// sample, attached to the child before it execs and read once it has exited.
// PMC_F_DESCENDANTS makes it follow the child's own children, matching the
// `inherit` flag the Linux backend sets.
//
// Requires the hwpmc(4) module (`kldload hwpmc`) and, on most systems,
// security.bsd.unprivileged_proc_debug left at its default so a user can attach
// a PMC to a process they own.
//
// Which events exist depends entirely on the CPU. libpmc exposes portable
// aliases for some of them ("instructions", "unhalted-cycles",
// "branch-mispredicts") but not for all, and the cache events in particular are
// only aliased on some AMD parts. So rather than hardcoding one name per
// counter, each counter has a list of candidate event specs that is probed once
// at startup; the first that allocates wins, and a counter with no working
// candidate is reported as unsupported. The same probe catches a CPU with fewer
// programmable PMC slots than the five counters poop would like, since the
// candidates are allocated together and later ones simply fail.
#include "../counters.h"

#include <errno.h>
#include <pmc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PMC_CPU_ANY
#define PMC_CPU_ANY (~0)
#endif

// Candidate event specs per counter, most portable first, NULL-terminated.
// libpmc's portable aliases ("instructions", "unhalted-cycles",
// "branch-mispredicts", "cache-references") cover only some CPUs, so each list
// falls back to the raw event names the x86 and ARMv8 PMU classes use. Matching
// is case-insensitive. Where a counter has no single obvious equivalent, the
// candidates follow the same mapping Linux's own perf events use on that
// architecture, so a poopc run means the same thing on both.
//
// "cycles" is deliberately last: on several Intel parts libpmc aliases it to the
// TSC, which is a system-scope-only PMC and so fails to allocate here, leaving
// the real alias "unhalted-cycles" to win.
static const char *const event_candidates[POOP_COUNTER_COUNT][6] = {
    [COUNTER_CPU_CYCLES] = {"unhalted-cycles", "cpu_clk_unhalted.thread_p",
                            "cpu_cycles", "cycles", NULL},
    [COUNTER_INSTRUCTIONS] = {"instructions", "inst_retired.any_p",
                              "inst_retired", "instr_executed", NULL},
    [COUNTER_CACHE_REFERENCES] = {"cache-references", "longest_lat_cache.reference",
                                  "llc-reference", "l1d_cache", "mem_access", NULL},
    [COUNTER_CACHE_MISSES] = {"cache-misses", "longest_lat_cache.miss",
                              "llc-misses", "dc-misses", "l1d_cache_refill", NULL},
    [COUNTER_BRANCH_MISSES] = {"branch-mispredicts", "br_misp_retired.all_branches",
                               "br_mis_pred", "branch_mispred", NULL},
};
struct Counters {
    const char *event[POOP_COUNTER_COUNT]; // the candidate that won the probe
    pmc_id_t id[POOP_COUNTER_COUNT];
    bool allocated[POOP_COUNTER_COUNT];
    bool started[POOP_COUNTER_COUNT];
    pid_t pid;
    CounterReadings readings;
    CounterSupport support;
    char reason[128];
};

static void drop_all(Counters *c, const char *why) {
    memset(&c->support, 0, sizeof(c->support));
    snprintf(c->reason, sizeof(c->reason), "%s", why);
}

static void release_all(Counters *c) {
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) {
        if (!c->allocated[i]) continue;
        if (c->started[i]) pmc_stop(c->id[i]);
        pmc_release(c->id[i]);
        c->allocated[i] = false;
        c->started[i] = false;
    }
}

const char *counters_backend_name(void) { return "hwpmc"; }

Counters *counters_open(void) {
    Counters *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->pid = -1;

    if (pmc_init() < 0) {
        drop_all(c, strerror(errno)); // usually ENOENT: hwpmc(4) is not loaded
        return c;
    }

    // Probe every counter with all allocations held open at once, so a CPU with
    // too few PMC slots is discovered here rather than mid-run.
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) {
        for (const char *const *spec = event_candidates[i]; *spec; spec++) {
            if (pmc_allocate(*spec, PMC_MODE_TC, PMC_F_DESCENDANTS, PMC_CPU_ANY,
                             &c->id[i], 0) < 0)
                continue;
            c->allocated[i] = true;
            c->event[i] = *spec;
            c->support.v[i] = true;
            break;
        }
    }
    release_all(c);

    if (!counters_any_supported(c))
        drop_all(c, "no usable hwpmc events for this CPU");
    return c;
}

void counters_close(Counters *c) {
    if (!c) return;
    release_all(c);
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
    c->pid = -1;
    memset(&c->readings, 0, sizeof(c->readings));
    release_all(c);

    for (int i = 0; i < POOP_COUNTER_COUNT; i++) {
        if (!c->support.v[i]) continue;
        if (pmc_allocate(c->event[i], PMC_MODE_TC, PMC_F_DESCENDANTS, PMC_CPU_ANY,
                         &c->id[i], 0) < 0) {
            // The probe said this event works, so a refusal now is a lost slot
            // or a revoked permission; drop the counter instead of the run.
            c->support.v[i] = false;
            snprintf(c->reason, sizeof(c->reason), "%s", strerror(errno));
            continue;
        }
        c->allocated[i] = true;
    }
}

void counters_child_spawned(Counters *c, pid_t pid) {
    c->pid = pid;
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) {
        if (!c->allocated[i]) continue;
        if (pmc_attach(c->id[i], pid) < 0 || pmc_start(c->id[i]) < 0) {
            c->support.v[i] = false;
            snprintf(c->reason, sizeof(c->reason), "%s", strerror(errno));
            continue;
        }
        c->started[i] = true;
    }
}

void counters_before_reap(Counters *c) {
    // The child has exited but is not reaped yet, so the PMCs still hold its
    // totals. hwpmc detaches on exit, so there is no pmc_detach to make here.
    for (int i = 0; i < POOP_COUNTER_COUNT; i++) {
        if (!c->started[i]) continue;
        pmc_value_t value = 0;
        if (pmc_read(c->id[i], &value) == 0) c->readings.v[i] = (uint64_t)value;
        pmc_stop(c->id[i]);
        c->started[i] = false;
    }
}

CounterReadings counters_after_reap(Counters *c) {
    release_all(c);
    return c->readings;
}
