#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "counters.h"

// Outcome of running a benchmarked command once.
typedef struct {
    bool exited;               // true: normal exit; false: killed by signal etc.
    int exit_code;             // valid when `exited`
    uint64_t wall_time;        // nanoseconds, measured around the child's own run
    uint64_t peak_rss;         // bytes (0 if unknown)
    CounterReadings counters;  // zero for whatever the backend does not support
    unsigned char *stderr_buf; // captured child stderr, NUL-terminated (malloc'd)
    size_t stderr_len;
    bool stderr_truncated;     // child wrote more stderr than we kept
} ChildResult;

// Runs argv once with stdin inherited, stdout discarded and stderr captured
// (bounded), driving `c` through one full counter cycle around it. The child is
// held between fork and exec until the pid-scoped backends have attached, so the
// wall time covers the command itself and not poop's bookkeeping.
//
// Returns 0 and fills *out when the child ran to completion (any exit status).
// Returns -1 with errno set when the child could not be spawned or exec'd.
int child_run(char *const argv[], Counters *c, ChildResult *out);
void child_result_free(ChildResult *r);
