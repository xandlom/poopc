// Hardware performance counters, abstracted over the platform that provides them.
//
// The backend is picked by the build script (see mate.c) from the sources in
// src/counters/, all of which implement this header. One benchmark run drives it
// like this:
//
//     Counters *c = counters_open();          // once, before the sampling loop
//
//     counters_prepare(c);                    // arm counters; must precede the fork
//     pid_t pid = fork();                     // child blocks before exec
//     counters_child_spawned(c, pid);         // attach pid-scoped counters
//     // ...release the child, drain its output...
//     counters_before_reap(c);                // latch pid-scoped data
//     wait4(pid, ...);                        // reaps the child
//     CounterReadings r = counters_after_reap(c);
//
//     counters_close(c);
//
// The hooks sit on both sides of the reap because platforms need opposite
// orderings. Linux reads counters through file descriptors that outlive the
// child, so it samples after the reap. Darwin and FreeBSD address them by pid
// and the accounting disappears the moment the zombie is collected, so they must
// sample before. The caller guarantees the child has already exited by then --
// see the waitid(WEXITED | WNOWAIT) in child.c.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "poop.h"

// The five hardware counters poop reports, in the order upstream prints them.
// COUNTER_NONE marks a measurement that does not come from a hardware counter
// (wall time, peak RSS) and is therefore always available.
typedef enum {
    COUNTER_NONE = -1,
    COUNTER_CPU_CYCLES = 0,
    COUNTER_INSTRUCTIONS,
    COUNTER_CACHE_REFERENCES,
    COUNTER_CACHE_MISSES,
    COUNTER_BRANCH_MISSES,
} CounterId;

// Counter values for a single run. Entries the backend does not support stay 0.
typedef struct {
    uint64_t v[POOP_COUNTER_COUNT];
} CounterReadings;

// Which entries the backend actually populates. Unsupported counters are omitted
// from the report entirely rather than printed as zeros. A backend may clear a
// flag mid-run if the counter stops being readable; poop.c consults this only
// once the sampling loop is done, so such a counter is dropped from the report.
typedef struct {
    bool v[POOP_COUNTER_COUNT];
} CounterSupport;

typedef struct Counters Counters;

// Opens the platform backend. Never fails: a platform with no counters, or a
// kernel that refuses them, yields a Counters with nothing supported, and poop
// degrades to wall time and peak RSS. Returns NULL only if out of memory.
Counters *counters_open(void);
void counters_close(Counters *c);

// Short name of the compiled-in backend, e.g. "perf_event_open".
const char *counters_backend_name(void);

const CounterSupport *counters_support(const Counters *c);
bool counters_any_supported(const Counters *c);

// Why no counter could be opened, in errno-message form, or NULL while at least
// one is still supported. Valid until the next call on `c`.
const char *counters_unavailable_reason(const Counters *c);

// Per-run hooks; see the sequence at the top of this file. They do not report
// failure: a backend that loses its counters clears the matching support flags
// and leaves the readings zeroed, which drops those rows from the report.
// counters_before_reap() is called with the child exited but not yet reaped.
void counters_prepare(Counters *c);
void counters_child_spawned(Counters *c, pid_t pid);
void counters_before_reap(Counters *c);
CounterReadings counters_after_reap(Counters *c);
