#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "child.h"
#include "counters.h"
#include "poop.h"
#include "progress.h"
#include "report.h"
#include "stats.h"
#include "term.h"

static const char usage_text[] =
    "Usage: poopc [options] <command1> ... <commandN>\n"
    "\n"
    "Compares the performance of the provided commands.\n"
    "\n"
    "Options:\n"
    " -d, --duration <ms>    (default: 5000) how long to repeatedly sample each command\n"
    " --color <when>         (default: auto) color output mode\n"
    "                            available options: 'auto', 'never', 'ansi'\n"
    " -f, --allow-failures   (default: false) compare performance if a non-zero exit code is returned\n";

// Compute + print order; matches upstream's Command.Measurements field order.
static const MeasurementDesc MEASUREMENT_DESCS[POOP_MEASUREMENT_COUNT] = {
    {"wall_time", offsetof(Sample, wall_time), offsetof(Measurements, wall_time),
     UNIT_NANOSECONDS, COUNTER_NONE},
    {"peak_rss", offsetof(Sample, peak_rss), offsetof(Measurements, peak_rss),
     UNIT_BYTES, COUNTER_NONE},
    {"cpu_cycles", offsetof(Sample, cpu_cycles),
     offsetof(Measurements, cpu_cycles), UNIT_COUNT, COUNTER_CPU_CYCLES},
    {"instructions", offsetof(Sample, instructions),
     offsetof(Measurements, instructions), UNIT_COUNT, COUNTER_INSTRUCTIONS},
    {"cache_references", offsetof(Sample, cache_references),
     offsetof(Measurements, cache_references), UNIT_COUNT, COUNTER_CACHE_REFERENCES},
    {"cache_misses", offsetof(Sample, cache_misses),
     offsetof(Measurements, cache_misses), UNIT_COUNT, COUNTER_CACHE_MISSES},
    {"branch_misses", offsetof(Sample, branch_misses),
     offsetof(Measurements, branch_misses), UNIT_COUNT, COUNTER_BRANCH_MISSES},
};

// True when this row can be filled in on this machine. Wall time and peak RSS
// always can; a hardware counter only if the backend populates it.
static bool desc_supported(const MeasurementDesc *d, const CounterSupport *s) {
    return d->counter == COUNTER_NONE || s->v[d->counter];
}

// Writes `count` U+2500 box-drawing dashes.
static void box_dashes(FILE *f, int count) {
    for (int i = 0; i < count; i++) fputs("\xe2\x94\x80", f);
}

// One framed line: <n dashes><label><n dashes>, or just dashes when label is NULL.
static void box_line(FILE *f, int n, const char *label) {
    box_dashes(f, n);
    if (label) {
        fputs(label, f);
        box_dashes(f, n);
    }
    fputc('\n', f);
}

// Splits a command string on spaces (skipping runs of spaces), building a
// NUL-terminated argv. No shell semantics, exactly like upstream.
static void parse_cmd(Command *cmd, const char *s) {
    size_t cap = 8;
    size_t n = 0;
    char **list = malloc(cap * sizeof(*list));

    const char *p = s;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ') p++;
        size_t len = (size_t)(p - start);
        if (n + 2 > cap) {
            cap *= 2;
            list = realloc(list, cap * sizeof(*list));
        }
        char *tok = malloc(len + 1);
        memcpy(tok, start, len);
        tok[len] = '\0';
        list[n++] = tok;
    }
    list[n] = NULL;
    cmd->argv = list;
    cmd->argc = n;
}

static Measurement *measurement_ptr(Measurements *ms, size_t offset) {
    return (Measurement *)((char *)ms + offset);
}

static uint64_t *sample_ptr(Sample *s, size_t offset) {
    return (uint64_t *)((char *)s + offset);
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    ColorMode color = COLOR_AUTO;
    uint64_t max_nano_seconds = 5000ull * 1000000ull;
    bool allow_failures = false;

    Command *commands = calloc((size_t)(argc > 1 ? argc : 1), sizeof(Command));
    size_t command_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-') {
            Command c;
            memset(&c, 0, sizeof(c));
            c.raw_cmd = arg;
            parse_cmd(&c, arg);
            if (c.argc == 0) {
                fprintf(stderr, "error: empty command\n");
                return 1;
            }
            commands[command_count++] = c;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            fputs(usage_text, stdout);
            fflush(stdout);
            return 0;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--duration") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr,
                        "'%s' requires a duration in milliseconds.\n%s", arg,
                        usage_text);
                return 1;
            }
            const char *next = argv[i];
            errno = 0;
            char *end = NULL;
            unsigned long long ms = strtoull(next, &end, 10);
            if (end == next || *end != '\0') {
                fprintf(stderr,
                        "unable to parse --duration argument '%s': %s\n", next,
                        "InvalidCharacter");
                return 1;
            }
            if (errno == ERANGE) {
                fprintf(stderr,
                        "unable to parse --duration argument '%s': %s\n", next,
                        "Overflow");
                return 1;
            }
            max_nano_seconds = 1000000ull * (uint64_t)ms;
        } else if (strcmp(arg, "--color") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr,
                        "'%s' requires a mode; options are 'auto', 'never', and "
                        "'ansi'.\n%s",
                        arg, usage_text);
                return 1;
            }
            const char *next = argv[i];
            if (strcmp(next, "auto") == 0) {
                color = COLOR_AUTO;
            } else if (strcmp(next, "never") == 0) {
                color = COLOR_NEVER;
            } else if (strcmp(next, "ansi") == 0) {
                color = COLOR_ANSI;
            } else {
                fprintf(stderr,
                        "unable to parse --color argument '%s'\n\navailable "
                        "options are 'auto', 'never' and 'ansi'\n",
                        next);
                return 1;
            }
        } else if (strcmp(arg, "-f") == 0 ||
                   strcmp(arg, "--allow-failures") == 0) {
            allow_failures = true;
        } else {
            fprintf(stderr, "unrecognized argument: '%s'\n%s", arg, usage_text);
            return 1;
        }
    }

    if (command_count == 0) {
        fputs(usage_text, stdout);
        fflush(stdout);
        return 1;
    }

    ProgressBar bar;
    progress_init(&bar, STDOUT_FILENO);

    Term term;
    term.out = stdout;
    switch (color) {
        case COLOR_AUTO: {
            const char *nc = getenv("NO_COLOR");
            const char *cf = getenv("CLICOLOR_FORCE");
            term.mode = term_detect_mode(STDOUT_FILENO, nc && nc[0] != '\0',
                                         cf && cf[0] != '\0');
            break;
        }
        case COLOR_NEVER:
            term.mode = TERM_NO_COLOR;
            break;
        case COLOR_ANSI:
            term.mode = TERM_ESCAPE_CODES;
            break;
    }

    Counters *counters = counters_open();
    if (!counters) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }
    // Warn once if the report will be missing every hardware counter, either
    // because this platform has no backend or because the kernel refused.
    bool warned_no_counters = !counters_any_supported(counters);
    if (warned_no_counters) {
        fprintf(stderr,
                "warning: hardware performance counters unavailable (%s); "
                "reporting wall time and peak RSS only\n",
                counters_unavailable_reason(counters));
    }

    size_t lost_samples = 0;

    static Sample samples_buf[POOP_MAX_SAMPLES];

    for (size_t cn = 0; cn < command_count; cn++) {
        Command *command = &commands[cn];
        size_t command_n = cn + 1;
        memset(&command->measurements, 0, sizeof(command->measurements));

        uint64_t first_start = now_ns();
        size_t sample_index = 0;
        while ((sample_index < POOP_MIN_SAMPLES ||
                (now_ns() - first_start) < max_nano_seconds) &&
               sample_index < POOP_MAX_SAMPLES) {
            if (term.mode != TERM_NO_COLOR) progress_render(&bar);

            ChildResult cr;
            if (child_run(command->argv, counters, &cr) != 0) {
                if (term.mode != TERM_NO_COLOR) progress_clear(&bar);
                fprintf(stderr, "\nerror: Couldn't execute %s: %s\n",
                        command->argv[0], strerror(errno));
                return 1;
            }

            if (!warned_no_counters && !counters_any_supported(counters)) {
                warned_no_counters = true;
                if (term.mode != TERM_NO_COLOR) progress_clear(&bar);
                fprintf(stderr,
                        "warning: hardware performance counters unavailable "
                        "(%s); reporting wall time and peak RSS only\n",
                        counters_unavailable_reason(counters));
            }

            if (cr.exited) {
                if (cr.exit_code != 0 && !allow_failures) {
                    if (term.mode != TERM_NO_COLOR) progress_clear(&bar);
                    fprintf(stderr,
                            "\nerror: Benchmark %zu command '%s' failed with "
                            "exit code %d:\n",
                            command_n, command->raw_cmd, cr.exit_code);
                    if (cr.stderr_truncated) {
                        box_line(stderr, 14, " truncated stderr ");
                    } else {
                        box_line(stderr, 19, " stderr ");
                    }
                    fprintf(stderr, "%s\n", cr.stderr_buf);
                    box_line(stderr, 46, NULL);
                    return 1;
                }
            } else {
                fprintf(stderr, "error: terminated unexpectedly\n");
                return 1;
            }

            // The platform could not hand over this run's counts, so there is
            // no sample here to keep -- recording it would mean zeros in the
            // table, which drag the minimum down and inflate the deviation.
            // Drop it and run the command again.
            if (cr.counters.lost) {
                child_result_free(&cr);
                if (++lost_samples <= POOP_MAX_LOST_SAMPLES) continue;
                if (term.mode != TERM_NO_COLOR) progress_clear(&bar);
                fprintf(stderr,
                        "warning: hardware performance counters kept losing "
                        "counts (%zu runs discarded); reporting wall time and "
                        "peak RSS only\n",
                        lost_samples);
                counters_disable(counters, "counts repeatedly lost");
                warned_no_counters = true; // this message replaces the generic one
                continue;
            }

            Sample s;
            memset(&s, 0, sizeof(s));
            s.wall_time = cr.wall_time;
            s.peak_rss = cr.peak_rss;
            for (size_t d = 0; d < POOP_MEASUREMENT_COUNT; d++) {
                const MeasurementDesc *desc = &MEASUREMENT_DESCS[d];
                if (desc->counter != COUNTER_NONE)
                    *sample_ptr(&s, desc->sample_offset) = cr.counters.v[desc->counter];
            }
            samples_buf[sample_index] = s;

            child_result_free(&cr);

            if (term.mode != TERM_NO_COLOR) {
                uint64_t cur_samples = (uint64_t)sample_index + 1;
                uint64_t elapsed = now_ns() - first_start;
                uint64_t ns_per_sample = elapsed / cur_samples;
                if (ns_per_sample == 0) ns_per_sample = 1;
                uint64_t estimate =
                    (max_nano_seconds + ns_per_sample - 1) / ns_per_sample;
                uint64_t est = cur_samples;
                if (estimate > est) est = estimate;
                if (POOP_MIN_SAMPLES > est) est = POOP_MIN_SAMPLES;
                if (est > POOP_MAX_SAMPLES) est = POOP_MAX_SAMPLES;
                bar.estimate = est;
                bar.current += 1;
            }

            sample_index++;
        }

        if (term.mode != TERM_NO_COLOR) {
            progress_clear(&bar);
            bar.current = 0;
            bar.estimate = 1;
        }

        const CounterSupport *support = counters_support(counters);
        size_t n = sample_index;
        for (size_t d = 0; d < POOP_MEASUREMENT_COUNT; d++) {
            const MeasurementDesc *desc = &MEASUREMENT_DESCS[d];
            if (!desc_supported(desc, support)) continue;
            Measurement mm = measurement_compute(samples_buf, n,
                                                 desc->sample_offset, desc->unit);
            *measurement_ptr(&command->measurements, desc->measurement_offset) = mm;
        }
        command->sample_count = n;

        report_command_header(&term, command, command_n, command_count);
        for (size_t d = 0; d < POOP_MEASUREMENT_COUNT; d++) {
            const MeasurementDesc *desc = &MEASUREMENT_DESCS[d];
            if (!desc_supported(desc, support)) continue;
            Measurement *mm =
                measurement_ptr(&command->measurements, desc->measurement_offset);
            Measurement *first_mm =
                (command_n == 1)
                    ? NULL
                    : measurement_ptr(&commands[0].measurements,
                                      desc->measurement_offset);
            report_measurement(&term, mm, desc->name, first_mm, command_count);
        }
        fflush(stdout);
    }

    counters_close(counters);
    fflush(stdout);
    return 0;
}
