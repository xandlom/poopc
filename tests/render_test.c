// Golden-output regression test for the alignment-sensitive rendering code.
//
// tests/render_golden.txt was captured byte-for-byte from Andrew Kelley's `poop`
// (Zig, 0.16 branch) driving its printMeasurement with these exact fixed
// Measurement values, in `--color never` then `--color ansi` mode.
//
//   render_test          write the current rendering to stdout (to refresh the golden)
//   render_test check     diff it against tests/render_golden.txt; exit 1 on mismatch
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "report.h"

#define M(mn, mx, me, sd, oc, sc, un)                                          \
    (Measurement) {                                                            \
        .q1 = 0, .median = 0, .q3 = 0, .min = (mn), .max = (mx), .mean = (me), \
        .std_dev = (sd), .outlier_count = (oc), .sample_count = (sc),          \
        .unit = (un)                                                           \
    }

static const char *NAMES[7] = {
    "wall_time",        "peak_rss",     "cpu_cycles", "instructions",
    "cache_references", "cache_misses", "branch_misses",
};

static void run(FILE *out, TermMode mode) {
    Term t = {.out = out, .mode = mode};

    Measurement first[7] = {
        M(843000, 11800000, 1071000.0, 658000.0, 13, 280, UNIT_NANOSECONDS),
        M(1236992, 1236992, 1236992.0, 0.0, 0, 280, UNIT_BYTES),
        M(1000000, 9000000, 3100000.0, 500000.0, 4, 280, UNIT_COUNT),
        M(2000000, 8000000, 4400000.0, 300000.0, 2, 280, UNIT_COUNT),
        M(10000, 90000, 33000.0, 5000.0, 1, 280, UNIT_COUNT),
        M(100, 9000, 1234.0, 456.0, 7, 280, UNIT_COUNT),
        M(50, 900, 321.0, 88.0, 3, 280, UNIT_COUNT),
    };
    Measurement second[7] = {
        M(900000, 15000000, 1250000.0, 720000.0, 20, 265, UNIT_NANOSECONDS),
        M(1300000, 1400000, 1310000.0, 12000.0, 5, 265, UNIT_BYTES),
        M(1100000, 9500000, 3400000.0, 550000.0, 6, 265, UNIT_COUNT),
        M(2100000, 8200000, 4500000.0, 310000.0, 3, 265, UNIT_COUNT),
        M(11000, 92000, 34000.0, 5100.0, 2, 265, UNIT_COUNT),
        M(110, 9100, 1300.0, 460.0, 8, 265, UNIT_COUNT),
        M(55, 950, 330.0, 90.0, 4, 265, UNIT_COUNT),
    };

    for (int i = 0; i < 7; i++)
        report_measurement(&t, &first[i], NAMES[i], NULL, 2);
    for (int i = 0; i < 7; i++)
        report_measurement(&t, &second[i], NAMES[i], &first[i], 2);
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    *len = fread(buf, 1, (size_t)n, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "check") != 0) {
        run(stdout, TERM_NO_COLOR);
        run(stdout, TERM_ESCAPE_CODES);
        return 0;
    }

    char *got = NULL;
    size_t got_len = 0;
    FILE *mem = open_memstream(&got, &got_len);
    run(mem, TERM_NO_COLOR);
    run(mem, TERM_ESCAPE_CODES);
    fclose(mem);

    const char *golden_path = (argc > 2) ? argv[2] : "tests/render_golden.txt";
    size_t want_len = 0;
    char *want = slurp(golden_path, &want_len);
    if (!want) {
        fprintf(stderr, "render_test: cannot open %s\n", golden_path);
        return 2;
    }

    int ok = (got_len == want_len) && memcmp(got, want, got_len) == 0;
    if (!ok) {
        fprintf(stderr, "render_test: output does not match %s\n", golden_path);
        fprintf(stderr, "--- got (%zu bytes) ---\n%s\n", got_len, got);
        fprintf(stderr, "--- want (%zu bytes) ---\n%s\n", want_len, want);
        return 1;
    }
    printf("render_test: OK (%zu bytes match %s)\n", got_len, golden_path);
    return 0;
}
