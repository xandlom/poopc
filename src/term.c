#include "term.h"

#include <sys/ioctl.h>
#include <unistd.h>

TermMode term_detect_mode(int fd, bool no_color_env, bool clicolor_force_env) {
    if (clicolor_force_env) return TERM_ESCAPE_CODES;
    if (no_color_env) return TERM_NO_COLOR;
    return isatty(fd) ? TERM_ESCAPE_CODES : TERM_NO_COLOR;
}

void term_set_color(const Term *t, TermColor c) {
    if (t->mode == TERM_NO_COLOR) return;
    const char *seq = "";
    switch (c) {
        case C_RESET:         seq = "\x1b[0m";  break;
        case C_BOLD:          seq = "\x1b[1m";  break;
        case C_DIM:           seq = "\x1b[2m";  break;
        case C_GREEN:         seq = "\x1b[32m"; break;
        case C_BRIGHT_GREEN:  seq = "\x1b[92m"; break;
        case C_CYAN:          seq = "\x1b[36m"; break;
        case C_MAGENTA:       seq = "\x1b[35m"; break;
        case C_YELLOW:        seq = "\x1b[33m"; break;
        case C_BRIGHT_YELLOW: seq = "\x1b[93m"; break;
        case C_BRIGHT_RED:    seq = "\x1b[91m"; break;
    }
    fputs(seq, t->out);
}

size_t term_screen_width(int fd) {
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) != 0) return 0;
    return ws.ws_col;
}
