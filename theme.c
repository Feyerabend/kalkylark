/*
 * theme.c - phosphor themes.
 *
 * VisiCalc itself was monochrome; the green (or amber, or white) came
 * from the monitor's phosphor. Each theme therefore has one hue at three
 * intensities on a near-black background, and the border and cell cursor
 * are plain reverse video of that hue - just like on the real screens.
 */
#define _POSIX_C_SOURCE 200809L

#include "theme.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rgb { unsigned char r, g, b; };

struct phosphor {
    const char *name;
    struct rgb  bg, dim, norm, bright;      /* truecolor */
    int         x_bg, x_dim, x_norm, x_bright;  /* xterm 256-colour index */
    const char *a_dim, *a_norm, *a_bright;  /* 16-colour SGR codes */
};

static const struct phosphor phosphors[] = {
    /* P1 green: Apple Monitor II, IBM 5151 */
    { "green", {  6, 14,  6 }, { 24, 130, 24 }, { 51, 255, 51 }, { 180, 255, 180 },
      16, 28, 83, 157, "32", "92", "1;92" },
    /* P3 amber */
    { "amber", { 16, 10,  0 }, { 130, 88,  0 }, { 255, 176,  0 }, { 255, 214, 128 },
      16, 136, 214, 222, "33", "93", "1;93" },
    /* P4 paper white */
    { "white", { 14, 14, 14 }, { 128, 128, 128 }, { 212, 212, 212 }, { 255, 255, 255 },
      233, 244, 252, 231, "90", "37", "1;97" },
};

#define NPHOSPHOR (sizeof phosphors / sizeof phosphors[0])

static char seq[ROLE_COUNT][80];

int colour_detect(void)
{
    const char *ct = getenv("COLORTERM");
    const char *t = getenv("TERM");

    if (ct && (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0))
        return COLOUR_TRUE;
    if (t && strstr(t, "256color"))
        return COLOUR_256;
    return COLOUR_16;
}

const char *theme_names(void)
{
    return "plain, green, amber, white";
}

/* Build an SGR foreground (+ background) spec for one intensity. */
static void colour_spec(char *out, size_t size, int level, const struct phosphor *p,
                        const struct rgb *fg, int x_fg, const char *a_fg)
{
    switch (level) {
    case COLOUR_TRUE:
        snprintf(out, size, "38;2;%d;%d;%d;48;2;%d;%d;%d",
                 fg->r, fg->g, fg->b, p->bg.r, p->bg.g, p->bg.b);
        break;
    case COLOUR_256:
        snprintf(out, size, "38;5;%d;48;5;%d", x_fg, p->x_bg);
        break;
    default:
        snprintf(out, size, "%s;40", a_fg);
        break;
    }
}

int theme_init(const char *name, int level)
{
    const struct phosphor *p = NULL;
    char dim[48], norm[48], bright[48];
    size_t i;

    if (name == NULL || strcmp(name, "plain") == 0 || level == COLOUR_NONE) {
        if (name && strcmp(name, "plain") != 0) {
            for (i = 0; i < NPHOSPHOR; i++)
                if (strcmp(name, phosphors[i].name) == 0)
                    break;
            if (i == NPHOSPHOR)
                return -1;
        }
        strcpy(seq[ROLE_NORMAL], "\033[0m");
        strcpy(seq[ROLE_DIM],    "\033[0m");
        strcpy(seq[ROLE_BRIGHT], "\033[0;1m");
        strcpy(seq[ROLE_BORDER], "\033[0;7m");
        strcpy(seq[ROLE_CURSOR], "\033[0;1;7m");
        return 0;
    }

    for (i = 0; i < NPHOSPHOR; i++)
        if (strcmp(name, phosphors[i].name) == 0)
            p = &phosphors[i];
    if (p == NULL)
        return -1;

    colour_spec(dim,    sizeof dim,    level, p, &p->dim,    p->x_dim,    p->a_dim);
    colour_spec(norm,   sizeof norm,   level, p, &p->norm,   p->x_norm,   p->a_norm);
    colour_spec(bright, sizeof bright, level, p, &p->bright, p->x_bright, p->a_bright);

    snprintf(seq[ROLE_NORMAL], sizeof seq[0], "\033[0;%sm",   norm);
    snprintf(seq[ROLE_DIM],    sizeof seq[0], "\033[0;%sm",   dim);
    snprintf(seq[ROLE_BRIGHT], sizeof seq[0], "\033[0;1;%sm", bright);
    snprintf(seq[ROLE_BORDER], sizeof seq[0], "\033[0;7;%sm", dim);
    snprintf(seq[ROLE_CURSOR], sizeof seq[0], "\033[0;7;%sm", bright);
    return 0;
}

void theme_use(enum role r)
{
    term_puts(seq[r]);
}
