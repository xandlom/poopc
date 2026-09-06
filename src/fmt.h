#pragma once

#include <math.h>
#include <stddef.h>
#include <stdio.h>

// snprintf a non-negative double at fixed precision using round-half-away-from-
// zero, matching Zig's std.fmt. C's printf rounds halves to even, which would
// shift a value like 2.5% to "2%" where upstream poop shows "3%" and, through
// the width, nudge whole columns out of alignment.
static inline int fmt_fixed(char *buf, size_t cap, int width, int prec,
                            double v) {
    double scale = 1.0;
    for (int i = 0; i < prec; i++) scale *= 10.0;
    double r = round(v * scale) / scale;
    return snprintf(buf, cap, "%*.*f", width, prec, r);
}
