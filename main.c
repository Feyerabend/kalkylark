/*
 * Kalkylark - a VisiCalc-style spreadsheet for VT100 terminals.
 * C99 + POSIX.1-2008, no curses.
 */
#define _POSIX_C_SOURCE 200809L

#include "term.h"
#include "theme.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
    fprintf(f,
        "usage: kalkylark [-p phosphor] [-c colours] [file]\n"
        "  -p  phosphor theme: %s (default: plain,\n"
        "      or $KALKYLARK_PHOSPHOR)\n"
        "  -c  colour depth: 16, 256 or true (default: detected)\n"
        "  -V  print version\n", theme_names());
}

int main(int argc, char **argv)
{
    const char *phosphor = getenv("KALKYLARK_PHOSPHOR");
    const char *term = getenv("TERM");
    int level = colour_detect();
    int opt;

    /* https://no-color.org: an explicit -p still wins */
    if (getenv("NO_COLOR") && *getenv("NO_COLOR"))
        phosphor = NULL;

    while ((opt = getopt(argc, argv, "p:c:Vh")) != -1) {
        switch (opt) {
        case 'p':
            phosphor = optarg;
            break;
        case 'c':
            if (strcmp(optarg, "16") == 0)        level = COLOUR_16;
            else if (strcmp(optarg, "256") == 0)  level = COLOUR_256;
            else if (strcmp(optarg, "true") == 0) level = COLOUR_TRUE;
            else { usage(stderr); return 2; }
            break;
        case 'V':
            printf("kalkylark %s\n", KALK_VERSION);
            return 0;
        case 'h':
            usage(stdout);
            return 0;
        default:
            usage(stderr);
            return 2;
        }
    }

    if (theme_init(phosphor, level) < 0) {
        fprintf(stderr, "kalkylark: unknown phosphor '%s' (try: %s)\n",
                phosphor, theme_names());
        return 2;
    }
    if (term && strcmp(term, "dumb") == 0) {
        fprintf(stderr, "kalkylark: needs a VT100-compatible terminal\n");
        return 1;
    }
    if (term_init() < 0) {
        fprintf(stderr, "kalkylark: stdin and stdout must be a terminal\n");
        return 1;
    }
    ui_run(optind < argc ? argv[optind] : NULL);
    term_done();
    return 0;
}
