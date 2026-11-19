/*
 * term.h - minimal terminal layer: POSIX termios for input,
 * ANSI/VT100 (ECMA-48) escape sequences for output.
 */
#ifndef KALK_TERM_H
#define KALK_TERM_H

#include <stddef.h>

/* Special keys returned by term_getkey(); plain bytes are 0..255. */
enum {
    KEY_ESC = 256,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_RESIZE,     /* terminal window changed size */
    KEY_UNKNOWN,    /* unrecognised escape sequence */
    KEY_EOF         /* input closed */
};

#define KEY_CTRL(c) ((c) & 0x1f)

int  term_init(void);           /* raw mode + alternate screen; -1 if not a tty */
void term_done(void);           /* restore terminal; safe to call twice */
void term_size(int *rows, int *cols);
int  term_getkey(void);

/* Output is buffered until term_flush(). Coordinates are 0-based. */
void term_write(const char *s, size_t n);
void term_puts(const char *s);
void term_putc(int c);
void term_move(int row, int col);
void term_clear(void);
void term_clear_eol(void);
void term_flush(void);

#endif
