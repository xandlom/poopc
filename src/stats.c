#include "stats.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// 95% two-sided critical values of Student's t, df 1..30.
static const double t_table95_1to30[30] = {
    12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
    2.201,  2.179, 2.16,  2.145, 2.131, 2.12,  2.11,  2.101, 2.093, 2.086,
    2.08,   2.074, 2.069, 2.064, 2.06,  2.056, 2.052, 2.045, 2.048, 2.042,
};

// 95% two-sided critical values of Student's t, df 10,20,...,120.
static const double t_table95_10s_10to120[12] = {
    2.228, 2.086, 2.042, 2.021, 2.009, 2, 1.994, 1.99, 1.987, 1.984, 1.982, 1.98,
};

double stat_score_95(uint64_t df) {
    if (df != 0) {
        if (df <= 30) {
            return t_table95_1to30[df - 1];
        } else if (df <= 120) {
            uint64_t idx_10s = df / 10;
            return t_table95_10s_10to120[idx_10s - 1];
        }
    }
    return 1.96;
}

static uint64_t field_at(const Sample *s, size_t offset) {
    uint64_t v;
    memcpy(&v, (const char *)s + offset, sizeof(v));
    return v;
}

// qsort is not reentrant across a captured offset; the program is single
// threaded so a file-static is fine.
static size_t g_sort_offset;

static int sample_less(const void *a, const void *b) {
    uint64_t lhs = field_at((const Sample *)a, g_sort_offset);
    uint64_t rhs = field_at((const Sample *)b, g_sort_offset);
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
}

Measurement measurement_compute(Sample *samples, size_t n, size_t sample_offset,
                                Unit unit) {
    g_sort_offset = sample_offset;
    qsort(samples, n, sizeof(*samples), sample_less);

    uint64_t total = 0;
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t v = field_at(&samples[i], sample_offset);
        total += v;
        if (v < min) min = v;
        if (v > max) max = v;
    }

    double mean = (double)total / (double)n;
    double std_dev = 0;
    for (size_t i = 0; i < n; i++) {
        double delta = (double)field_at(&samples[i], sample_offset) - mean;
        std_dev += delta * delta;
    }
    if (n > 1) {
        std_dev /= (double)(n - 1);
        std_dev = sqrt(std_dev);
    }

    uint64_t q1 = field_at(&samples[n / 4], sample_offset);
    uint64_t q3 = (n < 4) ? field_at(&samples[n - 1], sample_offset)
                          : field_at(&samples[n - n / 4], sample_offset);
    uint64_t median = field_at(&samples[n / 2], sample_offset);

    // Tukey's fences.
    double iqr = (double)(q3 - q1);
    double low_fence = (double)q1 - 1.5 * iqr;
    double high_fence = (double)q3 + 1.5 * iqr;
    uint64_t outlier_count = 0;
    for (size_t i = 0; i < n; i++) {
        double v = (double)field_at(&samples[i], sample_offset);
        if (v < low_fence || v > high_fence) outlier_count++;
    }

    return (Measurement){
        .q1 = q1,
        .median = median,
        .q3 = q3,
        .min = min,
        .max = max,
        .mean = mean,
        .std_dev = std_dev,
        .outlier_count = outlier_count,
        .sample_count = n,
        .unit = unit,
    };
}
