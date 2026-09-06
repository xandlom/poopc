#pragma once

#include <stdint.h>

// Animated spinner + progress bar drawn on a single terminal line, matching
// upstream poop's look. All output goes straight to the fd (bypassing stdio),
// so callers must flush stdout before rendering.
typedef struct {
    int frame_idx;
} Spinner;

typedef struct {
    Spinner spinner;
    uint64_t current;  // completed samples
    uint64_t estimate; // projected total samples (>= 1)
    int stdout_fd;
    uint64_t last_rendered_ns;
} ProgressBar;

void progress_init(ProgressBar *b, int stdout_fd);

// Clears the line, then redraws if at least 50ms have passed since the last draw.
void progress_render(ProgressBar *b);

// Erases the current line.
void progress_clear(ProgressBar *b);
