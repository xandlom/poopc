#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "poop.h"

// A group of hardware performance counters, fds[0] being the group leader.
typedef struct {
    int fds[POOP_PERF_COUNT];
} PerfGroup;

// Opens a fresh counter group armed to start counting on the child's exec.
// Returns true on success. On failure: returns false with errno set and every
// fd already closed; the caller should give up on perf for the rest of the run.
bool perf_group_open(PerfGroup *g);

void perf_group_reset(PerfGroup *g);   // DISABLE then RESET the group
void perf_group_disable(PerfGroup *g); // DISABLE the group
void perf_group_close(PerfGroup *g);   // close every fd, set to -1
uint64_t perf_read(int fd);            // read one u64 counter (0 on short read)

// Outcome of running a benchmarked command once.
typedef struct {
    bool exited;               // true: normal exit; false: killed by signal etc.
    int exit_code;             // valid when `exited`
    uint64_t peak_rss;         // bytes (0 if unknown)
    unsigned char *stderr_buf; // captured child stderr, NUL-terminated (malloc'd)
    size_t stderr_len;
    bool stderr_truncated;     // child wrote more stderr than we kept
} ChildResult;

// Runs argv with stdin inherited, stdout discarded, stderr captured (bounded).
// Returns 0 and fills *out when the child ran to completion (any exit status).
// Returns -1 with errno set when the child could not be exec'd.
int spawn_and_wait(char *const argv[], ChildResult *out);
void child_result_free(ChildResult *r);
