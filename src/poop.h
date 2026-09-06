// poopc - Performance Optimizer Observation Platform, C port of Andrew Kelley's
// `poop` (https://github.com/andrewrk/poop). Runs on Linux, macOS and FreeBSD;
// see src/counters.h for how the hardware counters are obtained on each.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define POOP_MAX_SAMPLES 10000
#define POOP_MIN_SAMPLES 3
#define POOP_COUNTER_COUNT 5

// Unit of a measured quantity, controls how values are rendered.
typedef enum {
    UNIT_NANOSECONDS,
    UNIT_BYTES,
    UNIT_COUNT,
} Unit;

// One timed execution of a command.
typedef struct {
    uint64_t wall_time; // nanoseconds
    uint64_t cpu_cycles;
    uint64_t instructions;
    uint64_t cache_references;
    uint64_t cache_misses;
    uint64_t branch_misses;
    uint64_t peak_rss; // bytes
} Sample;

// Aggregate statistics for one measured quantity across all samples.
typedef struct {
    uint64_t q1;
    uint64_t median;
    uint64_t q3;
    uint64_t min;
    uint64_t max;
    double mean;
    double std_dev;
    uint64_t outlier_count;
    uint64_t sample_count;
    Unit unit;
} Measurement;

// Field order here matches upstream's print order.
typedef struct {
    Measurement wall_time;
    Measurement peak_rss;
    Measurement cpu_cycles;
    Measurement instructions;
    Measurement cache_references;
    Measurement cache_misses;
    Measurement branch_misses;
} Measurements;

typedef struct {
    const char *raw_cmd;
    char **argv; // NULL-terminated, suitable for execvp
    size_t argc;
    Measurements measurements;
    size_t sample_count;
} Command;

typedef enum {
    COLOR_AUTO,
    COLOR_NEVER,
    COLOR_ANSI,
} ColorMode;

// Describes how to compute and where to store one measured quantity.
typedef struct {
    const char *name;
    size_t sample_offset;      // offsetof(Sample, <field>)
    size_t measurement_offset; // offsetof(Measurements, <field>)
    Unit unit;
    int counter; // CounterId of the backing counter, or COUNTER_NONE
} MeasurementDesc;

#define POOP_MEASUREMENT_COUNT 7

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
