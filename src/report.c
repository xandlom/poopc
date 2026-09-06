#include "report.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "stats.h"

static void pad(FILE *out, long n) {
    for (long i = 0; i < n; i++) fputc(' ', out);
}

// Formats `num` to roughly three significant figures, right-aligned, mirroring
// upstream's printNum3SigFigs. Returns bytes written.
static int print_num_3sig(char *dst, size_t cap, double num) {
    if (num >= 1000 || round(num) == num) return fmt_fixed(dst, cap, 4, 0, num);
    if (num >= 100) return fmt_fixed(dst, cap, 4, 0, num);
    if (num >= 10) return fmt_fixed(dst, cap, 3, 1, num);
    return fmt_fixed(dst, cap, 3, 2, num);
}

// Formats a scaled value plus a 2-char unit suffix into `dst`. When color is on,
// the suffix is wrapped in dim-white / reset, and those bytes are part of the
// returned length (upstream counts them too, which is what keeps the columns
// aligned the way the screenshot shows).
static int print_unit(char *dst, size_t cap, double x, Unit unit,
                      bool color_enabled) {
    double num = x;
    double val;
    const char *ustr;

    if (num >= 1e12) {
        val = num / 1e12;
        ustr = (unit == UNIT_COUNT) ? "T " : (unit == UNIT_NANOSECONDS) ? "ks" : "TB";
    } else if (num >= 1e9) {
        val = num / 1e9;
        ustr = (unit == UNIT_COUNT) ? "G " : (unit == UNIT_NANOSECONDS) ? "s " : "GB";
    } else if (num >= 1e6) {
        val = num / 1e6;
        ustr = (unit == UNIT_COUNT) ? "M " : (unit == UNIT_NANOSECONDS) ? "ms" : "MB";
    } else if (num >= 1e3) {
        val = num / 1e3;
        ustr = (unit == UNIT_COUNT) ? "K " : (unit == UNIT_NANOSECONDS) ? "us" : "KB";
    } else {
        val = num;
        ustr = (unit == UNIT_COUNT) ? "  " : (unit == UNIT_NANOSECONDS) ? "ns" : "  ";
    }

    int n = print_num_3sig(dst, cap, val);
    if (n < 0) return n;
    int m;
    if (color_enabled) {
        m = snprintf(dst + n, cap - (size_t)n, "%s%s%s", ANSI_DIM ANSI_WHITE, ustr,
                     ANSI_RESET);
    } else {
        m = snprintf(dst + n, cap - (size_t)n, "%s", ustr);
    }
    return (m < 0) ? m : n + m;
}

void report_command_header(const Term *t, const Command *cmd, size_t command_n,
                           size_t total_commands) {
    FILE *w = t->out;

    term_set_color(t, C_BOLD);
    fprintf(w, "Benchmark %zu", command_n);
    term_set_color(t, C_DIM);
    fprintf(w, " (%zu runs)", cmd->sample_count);
    term_set_color(t, C_RESET);
    fputs(":", w);
    for (size_t i = 0; i < cmd->argc; i++) fprintf(w, " %s", cmd->argv[i]);
    fputs("\n", w);

    term_set_color(t, C_BOLD);
    fputs("  measurement", w);
    pad(w, 23 - (long)strlen("  measurement"));
    term_set_color(t, C_BRIGHT_GREEN);
    fputs("mean", w);
    term_set_color(t, C_RESET);
    term_set_color(t, C_BOLD);
    fputs(" \xc2\xb1 ", w); // " ± "
    term_set_color(t, C_GREEN);
    fputs("\xcf\x83", w); // σ
    term_set_color(t, C_RESET);

    term_set_color(t, C_BOLD);
    pad(w, 12);
    term_set_color(t, C_CYAN);
    fputs("min", w);
    term_set_color(t, C_RESET);
    term_set_color(t, C_BOLD);
    fputs(" \xe2\x80\xa6 ", w); // " … "
    term_set_color(t, C_MAGENTA);
    fputs("max", w);
    term_set_color(t, C_RESET);

    term_set_color(t, C_BOLD);
    pad(w, 20 - (long)strlen(" outliers"));
    term_set_color(t, C_BRIGHT_YELLOW);
    fputs("outliers", w);
    term_set_color(t, C_RESET);

    if (total_commands >= 2) {
        term_set_color(t, C_BOLD);
        pad(w, 9);
        fputs("delta", w);
        term_set_color(t, C_RESET);
    }

    fputs("\n", w);
}

void report_measurement(const Term *t, const Measurement *m, const char *name,
                        const Measurement *first_m, size_t command_count) {
    FILE *w = t->out;
    char buf[256];
    int count = 0;
    bool color_enabled = t->mode != TERM_NO_COLOR;
    int n;

    fprintf(w, "  %s", name);

    // "  (mean  ):".len == 11
    pad(w, 32 - (11 + (long)strlen(name) + 2));

    term_set_color(t, C_BRIGHT_GREEN);
    n = print_unit(buf, sizeof buf, m->mean, m->unit, color_enabled);
    fwrite(buf, 1, (size_t)n, w);
    count += n;
    term_set_color(t, C_RESET);
    fputs(" \xc2\xb1 ", w); // " ± "
    term_set_color(t, C_GREEN);
    n = print_unit(buf, sizeof buf, m->std_dev, m->unit, color_enabled);
    fwrite(buf, 1, (size_t)n, w);
    count += n;
    term_set_color(t, C_RESET);

    // "  measurement      ".len == 19
    pad(w, 64 - (19 + count + 3));
    count = 0;

    term_set_color(t, C_CYAN);
    n = print_unit(buf, sizeof buf, (double)m->min, m->unit, color_enabled);
    fwrite(buf, 1, (size_t)n, w);
    count += n;
    term_set_color(t, C_RESET);
    fputs(" \xe2\x80\xa6 ", w); // " … "
    term_set_color(t, C_MAGENTA);
    n = print_unit(buf, sizeof buf, (double)m->max, m->unit, color_enabled);
    fwrite(buf, 1, (size_t)n, w);
    count += n;
    term_set_color(t, C_RESET);

    pad(w, 46 - (count + 1));
    count = 0;

    double outlier_percent =
        (double)m->outlier_count / (double)m->sample_count * 100.0;
    term_set_color(t, outlier_percent >= 10 ? C_YELLOW : C_DIM);
    char oc_str[32], op_str[32];
    fmt_fixed(oc_str, sizeof oc_str, 4, 0, (double)m->outlier_count);
    fmt_fixed(op_str, sizeof op_str, 2, 0, outlier_percent);
    n = snprintf(buf, sizeof buf, "%s (%s%%)", oc_str, op_str);
    fwrite(buf, 1, (size_t)n, w);
    count += n;
    term_set_color(t, C_RESET);

    pad(w, 19 - (count + 1));

    if (command_count > 1) {
        if (first_m) {
            const Measurement *f = first_m;
            double z = stat_score_95(m->sample_count + f->sample_count - 2);
            double n1 = (double)m->sample_count;
            double n2 = (double)f->sample_count;
            double normer = sqrt(1.0 / n1 + 1.0 / n2);
            double numer1 = (n1 - 1) * (m->std_dev * m->std_dev);
            double numer2 = (n2 - 1) * (f->std_dev * f->std_dev);
            double df = n1 + n2 - 2;
            double sp = sqrt((numer1 + numer2) / df);
            double half = (z * sp * normer) * 100.0 / f->mean;
            double diff_mean_percent = (m->mean - f->mean) * 100.0 / f->mean;
            bool is_sig =
                (diff_mean_percent >= 1 && (diff_mean_percent - half) >= 1) ||
                (diff_mean_percent <= -1 && (diff_mean_percent + half) <= -1);

            if (m->mean > f->mean) {
                if (is_sig) {
                    fputs("\xf0\x9f\x92\xa9", w); // 💩
                    term_set_color(t, C_BRIGHT_RED);
                } else {
                    term_set_color(t, C_DIM);
                    fputs("  ", w);
                }
                fputs("+", w);
            } else {
                if (is_sig) {
                    term_set_color(t, C_BRIGHT_YELLOW);
                    fputs("\xe2\x9a\xa1", w); // ⚡
                    term_set_color(t, C_BRIGHT_GREEN);
                } else {
                    term_set_color(t, C_DIM);
                    fputs("  ", w);
                }
                fputs("-", w);
            }
            char dm_str[32], half_str[32];
            fmt_fixed(dm_str, sizeof dm_str, 5, 1, fabs(diff_mean_percent));
            fmt_fixed(half_str, sizeof half_str, 4, 1, half);
            n = snprintf(buf, sizeof buf, "%s%% \xc2\xb1 %s%%", dm_str, half_str);
            fwrite(buf, 1, (size_t)n, w);
            count += n;
        } else {
            term_set_color(t, C_DIM);
            fputs("0%", w);
        }
    }

    term_set_color(t, C_RESET);
    fputs("\n", w);
    (void)count;
}
