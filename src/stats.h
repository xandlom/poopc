#pragma once

#include "poop.h"

// Sort `samples` by the field at `sample_offset`, then compute quartiles, mean,
// sample standard deviation and a Tukey's-fences outlier count. `samples` is
// reordered in place.
Measurement measurement_compute(Sample *samples, size_t n, size_t sample_offset,
                                Unit unit);

// T (or Z) score for 95% confidence. `df` of 0 means "no degrees of freedom
// known" and yields the Z score 1.96.
double stat_score_95(uint64_t df);
