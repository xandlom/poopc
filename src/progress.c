#include "progress.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fmt.h"
#include "poop.h"
#include "term.h"

// 10 braille frames, 3 UTF-8 bytes each.
static const char spinner_frames[] = "\xe2\xa0\x8b\xe2\xa0\x99\xe2\xa0\xb9"
                                     "\xe2\xa0\xb8\xe2\xa0\xbc\xe2\xa0\xb4"
                                     "\xe2\xa0\xa6\xe2\xa0\xa7\xe2\xa0\x87"
                                     "\xe2\xa0\x8f";
#define SPINNER_FRAME_BYTES 3
#define SPINNER_FRAME_COUNT 10

static const char BAR_FULL[] = "\xe2\x94\x81";       // ━
static const char BAR_HALF_LEFT[] = "\xe2\x95\xb8";  // ╸
static const char BAR_HALF_RIGHT[] = "\xe2\x95\xba"; // ╺

#define RUNS_LABEL_LEN 12 // " 10000 runs "
#define PCT_LABEL_LEN 6   // " 100% "

static void spinner_next(Spinner *s) {
    s->frame_idx = (s->frame_idx + 1) % SPINNER_FRAME_COUNT;
}

static const char *spinner_get(const Spinner *s) {
    return &spinner_frames[(size_t)s->frame_idx * SPINNER_FRAME_BYTES];
}

static void write_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return;
        p += n;
        len -= (size_t)n;
    }
}

void progress_init(ProgressBar *b, int stdout_fd) {
    b->spinner.frame_idx = 0;
    b->current = 0;
    b->estimate = 1;
    b->stdout_fd = stdout_fd;
    b->last_rendered_ns = now_ns();
}

void progress_clear(ProgressBar *b) {
    fflush(stdout);
    write_all(b->stdout_fd, ANSI_ERASE_LINE, sizeof(ANSI_ERASE_LINE) - 1);
}

void progress_render(ProgressBar *b) {
    uint64_t now = now_ns();
    if ((now - b->last_rendered_ns) / 1000000ull < 50) return;

    progress_clear(b);
    b->last_rendered_ns = now;

    size_t width = term_screen_width(b->stdout_fd);
    if (width < SPINNER_FRAME_BYTES + RUNS_LABEL_LEN + PCT_LABEL_LEN + 2) return;

    char *buf = NULL;
    size_t buf_size = 0;
    FILE *ms = open_memstream(&buf, &buf_size);
    if (!ms) return;

    size_t bar_width = width - SPINNER_FRAME_BYTES - RUNS_LABEL_LEN - PCT_LABEL_LEN;
    uint64_t prog_len = (uint64_t)(bar_width * 2) * b->current / b->estimate;
    size_t full_bars_len = (size_t)(prog_len / 2);

    fprintf(ms, "%s%.*s%s %5" PRIu64 " runs ", ANSI_CYAN, SPINNER_FRAME_BYTES,
            spinner_get(&b->spinner), ANSI_RESET, b->current);
    spinner_next(&b->spinner);

    fputs(ANSI_PINK, ms);
    for (size_t i = 0; i < full_bars_len; i++) fputs(BAR_FULL, ms);
    if (prog_len % 2 == 1) fputs(BAR_HALF_LEFT, ms);
    fputs(ANSI_WHITE ANSI_DIM, ms);
    if (prog_len % 2 == 0) fputs(BAR_HALF_RIGHT, ms);
    for (size_t i = 0; i + full_bars_len + 1 < bar_width; i++) fputs(BAR_FULL, ms);
    fputs(ANSI_RESET, ms);
    char pct[16];
    fmt_fixed(pct, sizeof pct, 3, 0,
             (double)b->current * 100.0 / (double)b->estimate);
    fprintf(ms, " %s%% ", pct);

    fclose(ms);
    write_all(b->stdout_fd, buf, buf_size);
    free(buf);
}
