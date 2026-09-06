// Fallback backend for platforms with no counter support wired up yet.
//
// poop still works here, reporting wall time and peak RSS only. See
// src/counters.h for the interface a new backend has to implement, and
// src/counters/freebsd.c for the shape of one that has to discover at runtime
// which counters the hardware will give it.
#include "../counters.h"

#include <stdlib.h>
#include <string.h>

struct Counters {
    CounterSupport support; // all false
};

const char *counters_backend_name(void) { return "none"; }

Counters *counters_open(void) { return calloc(1, sizeof(Counters)); }
void counters_close(Counters *c) { free(c); }

const CounterSupport *counters_support(const Counters *c) { return &c->support; }
bool counters_any_supported(const Counters *c) {
    (void)c;
    return false;
}
const char *counters_unavailable_reason(const Counters *c) {
    (void)c;
    return "no counter backend for this platform";
}

void counters_disable(Counters *c, const char *why) {
    (void)c;
    (void)why; // nothing was ever enabled
}

void counters_prepare(Counters *c) { (void)c; }
void counters_child_spawned(Counters *c, pid_t pid) {
    (void)c;
    (void)pid;
}
void counters_before_reap(Counters *c) { (void)c; }
CounterReadings counters_after_reap(Counters *c) {
    (void)c;
    CounterReadings r;
    memset(&r, 0, sizeof(r));
    return r;
}
