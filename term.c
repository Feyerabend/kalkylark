/*
 * term.c - termios raw mode, key decoding and buffered VT100 output.
 *
 * No curses: everything here is POSIX.1-2008 plus the escape sequences
 * every VT100-descended terminal understands.
 */
#define _POSIX_C_SOURCE 200809L
/* SIGWINCH and TIOCGWINSZ are universal but only entered POSIX in 2024;
 * macOS hides them in strict POSIX mode unless asked. Both are guarded
 * with #ifdef below, so a system without them still builds. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "term.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define ESC_TIMEOUT_MS 50       /* how long to wait for the rest of a sequence */

static struct termios orig_tio;
static volatile sig_atomic_t raw_on;
static volatile sig_atomic_t resized;

static char   obuf[1 << 16];
static size_t olen;

/* ---- output ---------------------------------------------------------- */

static void write_all(const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = write(STDOUT_FILENO, s, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

void term_flush(void)
{
    write_all(obuf, olen);
    olen = 0;
}

void term_write(const char *s, size_t n)
{
    if (n > sizeof obuf) {
        term_flush();
        write_all(s, n);
        return;
    }
    if (olen + n > sizeof obuf)
        term_flush();
    memcpy(obuf + olen, s, n);
    olen += n;
}

void term_puts(const char *s)  { term_write(s, strlen(s)); }
void term_putc(int c)          { char ch = (char)c; term_write(&ch, 1); }
void term_clear(void)          { term_puts("\033[H\033[2J"); }
void term_clear_eol(void)      { term_puts("\033[K"); }

void term_move(int row, int col)
{
    char b[32];
    int n = snprintf(b, sizeof b, "\033[%d;%dH", row + 1, col + 1);
    if (n > 0)
        term_write(b, (size_t)n);
}

/* ---- setup / teardown ------------------------------------------------ */

void term_done(void)
{
    static const char reset[] = "\033[0m\033[?25h\033[?1049l";

    if (!raw_on)
        return;
    raw_on = 0;
    olen = 0;
    write_all(reset, sizeof reset - 1);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
}

static void on_winch(int sig)
{
    (void)sig;
    resized = 1;
}

static void on_fatal(int sig)
{
    term_done();                /* only async-signal-safe calls inside */
    signal(sig, SIG_DFL);
    raise(sig);
}

int term_init(void)
{
    struct termios raw;
    struct sigaction sa;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return -1;
    if (tcgetattr(STDIN_FILENO, &orig_tio) < 0)
        return -1;

    raw = orig_tio;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
        return -1;
    raw_on = 1;
    atexit(term_done);

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
#ifdef SIGWINCH
    sa.sa_handler = on_winch;   /* no SA_RESTART: we want read() to wake up */
    sigaction(SIGWINCH, &sa, NULL);
#endif
    sa.sa_handler = on_fatal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    /* alternate screen, hide cursor, clear */
    term_puts("\033[?1049h\033[?25l\033[H\033[2J");
    term_flush();
    return 0;
}

void term_size(int *rows, int *cols)
{
    const char *e;

    *rows = 24;
    *cols = 80;
#ifdef TIOCGWINSZ
    {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
            && ws.ws_row > 0 && ws.ws_col > 0) {
            *rows = ws.ws_row;
            *cols = ws.ws_col;
            return;
        }
    }
#endif
    if ((e = getenv("LINES")) != NULL && atoi(e) > 0)
        *rows = atoi(e);
    if ((e = getenv("COLUMNS")) != NULL && atoi(e) > 0)
        *cols = atoi(e);
}

/* ---- input ----------------------------------------------------------- */

/* Read one byte. timeout_ms < 0 blocks. Returns -1 on timeout/EOF,
 * -2 if a blocking read was interrupted by a window resize. */
static int read_byte(int timeout_ms)
{
    unsigned char c;

    if (timeout_ms >= 0) {
        struct pollfd p;
        p.fd = STDIN_FILENO;
        p.events = POLLIN;
        p.revents = 0;
        if (poll(&p, 1, timeout_ms) <= 0)
            return -1;
    }
    for (;;) {
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1)
            return c;
        if (n < 0 && errno == EINTR) {
            if (resized && timeout_ms < 0)
                return -2;
            continue;
        }
        return -1;
    }
}

static int decode_csi(void)
{
    char param[16];
    size_t n = 0;
    int c;

    /* parameter/intermediate bytes until a final byte 0x40..0x7e */
    for (;;) {
        c = read_byte(ESC_TIMEOUT_MS);
        if (c < 0)
            return KEY_ESC;
        if (c >= 0x40 && c <= 0x7e)
            break;
        if (n + 1 < sizeof param)
            param[n++] = (char)c;
    }
    param[n] = '\0';

    switch (c) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case '~':
        switch (atoi(param)) {
        case 1: case 7: return KEY_HOME;
        case 4: case 8: return KEY_END;
        case 2:         return KEY_INSERT;
        case 3:         return KEY_DELETE;
        case 5:         return KEY_PGUP;
        case 6:         return KEY_PGDN;
        }
        break;
    }
    return KEY_UNKNOWN;
}

int term_getkey(void)
{
    int c;

    if (resized) {
        resized = 0;
        return KEY_RESIZE;
    }
    c = read_byte(-1);
    if (c == -2) {
        resized = 0;
        return KEY_RESIZE;
    }
    if (c < 0)
        return KEY_EOF;

    switch (c) {
    case '\r': case '\n': return KEY_ENTER;
    case 127:  case 8:    return KEY_BACKSPACE;
    case 27:              break;
    default:              return c;
    }

    /* ESC alone, or the start of a sequence (ESC [ ... or ESC O x) */
    c = read_byte(ESC_TIMEOUT_MS);
    if (c < 0)
        return KEY_ESC;
    if (c == '[' || c == 'O')
        return decode_csi();
    return KEY_UNKNOWN;         /* Alt+key: ignore for now */
}
