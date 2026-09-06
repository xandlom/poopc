#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Raw SGR / control sequences also used when composing strings by hand.
#define ANSI_DIM        "\x1b[2m"
#define ANSI_WHITE      "\x1b[37m"
#define ANSI_PINK       "\x1b[38;5;205m"
#define ANSI_CYAN       "\x1b[36m"
#define ANSI_RESET      "\x1b[0m"
#define ANSI_ERASE_LINE "\x1b[2K\r"

typedef enum {
    TERM_NO_COLOR,
    TERM_ESCAPE_CODES,
} TermMode;

typedef enum {
    C_RESET,
    C_BOLD,
    C_DIM,
    C_GREEN,
    C_BRIGHT_GREEN,
    C_CYAN,
    C_MAGENTA,
    C_YELLOW,
    C_BRIGHT_YELLOW,
    C_BRIGHT_RED,
} TermColor;

typedef struct {
    FILE *out;
    TermMode mode;
} Term;

// Resolves `--color auto`: escape codes when CLICOLOR_FORCE is set, otherwise no
// color when NO_COLOR is set, otherwise escape codes only if `fd` is a tty.
TermMode term_detect_mode(int fd, bool no_color_env, bool clicolor_force_env);

// Writes the SGR sequence for `c` to `t->out`; a no-op when `t->mode` is
// TERM_NO_COLOR.
void term_set_color(const Term *t, TermColor c);

// Terminal width in columns via TIOCGWINSZ, or 0 if unavailable.
size_t term_screen_width(int fd);
