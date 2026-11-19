/*
 * theme.h - phosphor colour themes expressed as SGR escape sequences.
 */
#ifndef KALK_THEME_H
#define KALK_THEME_H

enum role {
    ROLE_NORMAL,    /* ordinary text and empty cells */
    ROLE_DIM,       /* secondary text */
    ROLE_BRIGHT,    /* status line, entry text */
    ROLE_BORDER,    /* inverse-video column/row border */
    ROLE_CURSOR,    /* inverse-video cell cursor */
    ROLE_COUNT
};

enum colour_level {
    COLOUR_NONE,    /* bold and reverse video only */
    COLOUR_16,
    COLOUR_256,
    COLOUR_TRUE
};

int  colour_detect(void);                       /* from COLORTERM / TERM */
int  theme_init(const char *name, int level);   /* -1 if name is unknown */
const char *theme_names(void);
void theme_use(enum role r);                    /* emit the role's SGR */

#endif
