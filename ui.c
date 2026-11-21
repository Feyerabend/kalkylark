/*
 * ui.c - screen layout and key handling, modelled on VisiCalc:
 *
 *   line 1  status:  current cell and its contents
 *   line 2  prompt:  command menus, "Label"/"Value", messages
 *   line 3  entry:   the line being typed
 *   line 4  inverse-video column border
 *   rest    inverse-video row numbers + the grid
 *
 * The whole screen is rebuilt into one buffer and written with a single
 * write(), which is cheap at terminal sizes and never flickers.
 */
#define _POSIX_C_SOURCE 200809L

#include "ui.h"
#include "sheet.h"
#include "term.h"
#include "theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TOP_LINES    4      /* status, prompt, entry, column border */
#define GUTTER       4      /* row-number column: "254 " */
#define ENTRY_MAX    250

static int cur_row, cur_col;        /* cell cursor */
static int top_row, left_col;       /* first visible row/column */
static int scr_rows, scr_cols;

static int pointing;                /* pointer replaces the cursor while */
static int ptr_row, ptr_col;        /* building a formula with arrow keys */

static char msg[160];               /* prompt line */
static const char *entry;           /* entry line, NULL when idle */

static void set_msg(const char *s)
{
    snprintf(msg, sizeof msg, "%s", s);
}

/* Write s into a field of the given width: align 0 left, 1 centre, 2 right. */
static void put_field(const char *s, int width, int align)
{
    int len = (int)strlen(s), pad, i;

    if (width <= 0)
        return;
    if (len > width)
        len = width;
    pad = width - len;
    if (align == 1)
        pad /= 2;
    else if (align == 0)
        pad = 0;
    for (i = 0; i < pad; i++)
        term_putc(' ');
    term_write(s, (size_t)len);
    for (i = pad + len; i < width; i++)
        term_putc(' ');
}

static int visible_cols(void)
{
    int n = (scr_cols - GUTTER) / sheet_colw;
    return n < 1 ? 1 : n;
}

static int visible_rows(void)
{
    int n = scr_rows - TOP_LINES;
    return n < 1 ? 1 : n;
}

/* The highlighted cell: the pointer while pointing, else the cursor. */
static int hl_row(void) { return pointing ? ptr_row : cur_row; }
static int hl_col(void) { return pointing ? ptr_col : cur_col; }

static void scroll_into_view(void)
{
    int nc = visible_cols(), nr = visible_rows();
    int r = hl_row(), c = hl_col();

    if (c < left_col)
        left_col = c;
    if (c >= left_col + nc)
        left_col = c - nc + 1;
    if (r < top_row)
        top_row = r;
    if (r >= top_row + nr)
        top_row = r - nr + 1;
}

static void draw_status(void)
{
    const struct cell *cl = sheet_cell(cur_row, cur_col);
    char b[ENTRY_MAX + 32], name[4];
    int len;

    term_move(0, 0);
    theme_use(ROLE_BRIGHT);
    len = snprintf(b, sizeof b, " %s%d", col_name(cur_col, name), cur_row + 1);
    if (cl->fmt)
        len += snprintf(b + len, sizeof b - (size_t)len, " /F%c", cl->fmt);
    if (cl->type == CELL_LABEL)
        snprintf(b + len, sizeof b - (size_t)len, " (L) %s", cl->text);
    else if (cl->type == CELL_VALUE)
        snprintf(b + len, sizeof b - (size_t)len, " (V) %s", cl->text);
    put_field(b, scr_cols, 0);
    len = (int)strlen(b);
    /* right side: [CIRC] order KALKYLARK */
    if (scr_cols > len + 20) {
        term_move(0, scr_cols - 19);
        theme_use(ROLE_BRIGHT);
        term_puts(sheet_circular ? "CIRC" : "    ");
        theme_use(ROLE_DIM);
        term_move(0, scr_cols - 13);
        term_putc(sheet_order);
        term_move(0, scr_cols - 10);
        term_puts("KALKYLARK");
    }
}

static void draw_entry(void)
{
    size_t len, room;
    const char *s;

    term_move(2, 0);
    if (entry) {
        /* keep the end of a long entry in view */
        room = scr_cols > 2 ? (size_t)scr_cols - 2 : 1;
        len = strlen(entry);
        s = len > room ? entry + (len - room) : entry;
        theme_use(ROLE_BRIGHT);
        term_puts(s);
        theme_use(ROLE_CURSOR);
        term_putc(' ');
    }
    theme_use(ROLE_NORMAL);
    term_clear_eol();
}

static void draw_grid(void)
{
    char b[64], name[4];
    int nc = visible_cols(), nr = visible_rows(), x, y, r, c, right;

    term_move(3, 0);
    theme_use(ROLE_BORDER);
    put_field("", GUTTER, 0);
    for (x = 0; x < nc && left_col + x < SHEET_COLS; x++)
        put_field(col_name(left_col + x, name), sheet_colw, 1);
    theme_use(ROLE_NORMAL);
    term_clear_eol();

    for (y = 0; y < nr; y++) {
        r = top_row + y;
        term_move(TOP_LINES + y, 0);
        if (r < SHEET_ROWS) {
            theme_use(ROLE_BORDER);
            snprintf(b, sizeof b, "%d", r + 1);
            put_field(b, GUTTER - 1, 2);
            term_putc(' ');
            for (x = 0; x < nc && left_col + x < SHEET_COLS; x++) {
                c = left_col + x;
                theme_use(r == hl_row() && c == hl_col() ? ROLE_CURSOR : ROLE_NORMAL);
                cell_display(sheet_cell(r, c), sheet_colw - 1, b, sizeof b, &right);
                if (right) {
                    put_field(b, sheet_colw - 1, 2);  /* one space before next column */
                    term_putc(' ');
                } else {
                    put_field(b, sheet_colw, 0);
                }
            }
        }
        theme_use(ROLE_NORMAL);
        term_clear_eol();
    }
}

static void draw(void)
{
    term_size(&scr_rows, &scr_cols);
    scroll_into_view();
    draw_status();

    term_move(1, 0);
    theme_use(ROLE_NORMAL);
    put_field(msg, scr_cols, 0);

    draw_entry();
    draw_grid();
    term_flush();
}

/* Show a prompt and wait for one key (uppercased); redraws on resize. */
static int ask_key(const char *prompt)
{
    int k;

    set_msg(prompt);
    do {
        draw();
        k = term_getkey();
    } while (k == KEY_RESIZE);
    set_msg("");
    return k < 128 ? toupper(k) : k;
}

/* Edit a line on the entry line, starting from buf's current contents:
 * Enter accepts it, typing replaces it, Backspace edits it.
 * Returns 1 on Enter, 0 if cancelled. */
static int ask_line(const char *prompt, char *buf, size_t size)
{
    size_t n = strlen(buf);
    int k, fresh = n > 0;

    set_msg(prompt);
    entry = buf;
    for (;;) {
        draw();
        k = term_getkey();
        if (k == KEY_ENTER)
            break;
        if (k == KEY_ESC || k == KEY_CTRL('c') || k == KEY_EOF) {
            buf[0] = '\0';
            break;
        }
        if (k == KEY_BACKSPACE) {
            fresh = 0;
            if (n > 0)
                buf[--n] = '\0';
        } else if (k >= 32 && k < 127) {
            if (fresh) {
                fresh = 0;
                n = 0;
            }
            if (n + 1 < size) {
                buf[n++] = (char)k;
                buf[n] = '\0';
            }
        }
    }
    entry = NULL;
    set_msg("");
    return k == KEY_ENTER;
}

static void clamp(int *row, int *col)
{
    if (*row < 0) *row = 0;
    if (*row >= SHEET_ROWS) *row = SHEET_ROWS - 1;
    if (*col < 0) *col = 0;
    if (*col >= SHEET_COLS) *col = SHEET_COLS - 1;
}

static void move_by(int dr, int dc)
{
    cur_row += dr;
    cur_col += dc;
    clamp(&cur_row, &cur_col);
}

/* Arrow-key deltas; returns 0 if k isn't an arrow. */
static int arrow(int k, int *dr, int *dc)
{
    *dr = *dc = 0;
    switch (k) {
    case KEY_UP:    *dr = -1; return 1;
    case KEY_DOWN:  *dr = 1;  return 1;
    case KEY_LEFT:  *dc = -1; return 1;
    case KEY_RIGHT: *dc = 1;  return 1;
    }
    return 0;
}

static int commit(const char *buf)
{
    const char *err;
    int pos;
    char m[160];

    if (sheet_enter(cur_row, cur_col, buf, &err, &pos) < 0) {
        snprintf(m, sizeof m, "Error: %s (at column %d)", err, pos + 1);
        set_msg(m);
        return 0;
    }
    sheet_recalc();
    return 1;
}

/*
 * Typing into a cell. The first key decides Label or Value. In a value,
 * after an operator, '(' or ',', the arrow keys point at a cell and insert
 * its coordinate; '.' while pointing makes a range (A1...). Otherwise an
 * arrow enters the cell and moves, like Enter plus a step.
 */
static void cell_entry(int first)
{
    char buf[ENTRY_MAX + 1], name[4];
    size_t n = 0, ref_at = 0;
    int k, dr, dc, value = sheet_is_value_start(first);
    int have_ptr = 0;

    buf[n++] = (char)first;
    buf[n] = '\0';
    entry = buf;
    pointing = 0;

    for (;;) {
        if (msg[0] == '\0' || strncmp(msg, "Error", 5) != 0)
            set_msg(pointing ? "Value: point at a cell"
                             : value ? "Value" : "Label");
        draw();
        k = term_getkey();
        if (k != KEY_RESIZE && strncmp(msg, "Error", 5) == 0)
            set_msg("");

        if (arrow(k, &dr, &dc)) {
            if (value && (pointing || strchr("+-*/^(,.", buf[n - 1]))) {
                if (!pointing) {
                    pointing = 1;
                    ref_at = n;
                    if (!have_ptr) {
                        ptr_row = cur_row;
                        ptr_col = cur_col;
                    }
                }
                have_ptr = 1;
                ptr_row += dr;
                ptr_col += dc;
                clamp(&ptr_row, &ptr_col);
                n = ref_at;
                n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%d",
                                      col_name(ptr_col, name), ptr_row + 1);
                if (n > ENTRY_MAX)
                    n = ENTRY_MAX;
                continue;
            }
            pointing = 0;
            if (commit(buf)) {
                move_by(dr, dc);
                break;
            }
            continue;
        }

        switch (k) {
        case KEY_ENTER:
            pointing = 0;
            if (commit(buf))
                goto done;
            break;
        case KEY_ESC:
        case KEY_CTRL('c'):
        case KEY_EOF:
            goto done;
        case KEY_BACKSPACE:
            pointing = 0;
            buf[--n] = '\0';
            if (n == 0)
                goto done;
            break;
        case KEY_RESIZE:
            theme_use(ROLE_NORMAL);
            term_clear();
            break;
        default:
            if (k < 32 || k >= 127)
                break;
            if (pointing && k == '.') {
                if (n + 3 <= ENTRY_MAX) {
                    memcpy(buf + n, "...", 4);
                    n += 3;
                }
                pointing = 0;
                break;
            }
            pointing = 0;
            if (n < ENTRY_MAX) {
                buf[n++] = (char)k;
                buf[n] = '\0';
            }
            break;
        }
    }
done:
    pointing = 0;
    entry = NULL;
    if (strncmp(msg, "Error", 5) != 0)
        set_msg("");
}

static void cmd_goto(void)
{
    char buf[16];
    int r, c;

    buf[0] = '\0';
    if (!ask_line("Go to: Coordinate", buf, sizeof buf) || buf[0] == '\0')
        return;
    if (parse_coord(buf, &r, &c) == 0) {
        cur_row = r;
        cur_col = c;
    } else {
        set_msg("Error: not a coordinate (A1 .. BK254)");
    }
}

static char filename[1024];         /* last file saved or loaded */

static int is_cancel(int k)
{
    return k == KEY_ESC || k == KEY_BACKSPACE || k == KEY_CTRL('c');
}

static int ask_filename(const char *prompt)
{
    return ask_line(prompt, filename, sizeof filename) && filename[0] != '\0';
}

static void cmd_save(void)
{
    char err[256], m[160];

    if (!ask_filename("Storage: Save file name"))
        return;
    if (access(filename, F_OK) == 0
        && ask_key("File exists: Type Y to overwrite") != 'Y')
        return;
    if (sheet_save(filename, err, sizeof err) == 0)
        snprintf(m, sizeof m, "Saved %s", filename);
    else
        snprintf(m, sizeof m, "Error: cannot save: %s", err);
    set_msg(m);
}

static void load_file(void)
{
    char err[256], m[160];

    if (sheet_load(filename, err, sizeof err) == 0) {
        snprintf(m, sizeof m, "Loaded %s", filename);
        cur_row = cur_col = top_row = left_col = 0;
    } else {
        snprintf(m, sizeof m, "Error: %s: %s", filename, err);
    }
    set_msg(m);
}

static void cmd_load(void)
{
    if (sheet_dirty && ask_key("Load: changes not saved. Type Y to continue") != 'Y')
        return;
    if (ask_filename("Storage: Load file name"))
        load_file();
}

static void cmd_format(int global)
{
    int k = ask_key(global ? "Global format: G I $ L R *"
                           : "Format: D G I $ L R *");

    if (is_cancel(k))
        return;
    if (!global && k == 'D')
        sheet_set_format(cur_row, cur_col, 0);
    else if (!sheet_valid_format(k))
        set_msg("Error: not a format");
    else if (global) {
        sheet_gfmt = k;
        sheet_dirty = 1;
    } else
        sheet_set_format(cur_row, cur_col, k);
}

static void cmd_colwidth(void)
{
    char buf[8], m[80];
    int w;

    snprintf(buf, sizeof buf, "%d", sheet_colw);
    snprintf(m, sizeof m, "Column width (%d..%d)", COLW_MIN, COLW_MAX);
    if (!ask_line(m, buf, sizeof buf))
        return;
    w = atoi(buf);
    if (w < COLW_MIN || w > COLW_MAX) {
        snprintf(m, sizeof m, "Error: width must be %d..%d", COLW_MIN, COLW_MAX);
        set_msg(m);
        return;
    }
    sheet_colw = w;
    sheet_dirty = 1;
    left_col = cur_col;     /* let scrolling settle from the cursor */
}

static void cmd_global(void)
{
    int k = ask_key("Global: C (column width) F (format) O (recalc order)");

    switch (k) {
    case 'C':
        cmd_colwidth();
        break;
    case 'F':
        cmd_format(1);
        break;
    case 'O':
        k = ask_key("Recalc order: R (by row) C (by column) N (natural)");
        if (k == 'R' || k == 'C' || k == 'N') {
            sheet_order = k;
            sheet_dirty = 1;
            sheet_recalc();
        }
        break;
    }
}

/* /I and /D: insert or delete the row or column at the cursor. Formulas
 * elsewhere are rewritten to follow the cells they read. */
static void cmd_rowcol(int del)
{
    char m[80], name[4];
    const char *err;
    int k = ask_key(del ? "Delete: R (row) C (column)"
                        : "Insert: R (row) C (column)");
    int at, rc;

    if (k != 'R' && k != 'C') {
        if (!is_cancel(k))
            set_msg("Error: type R or C");
        return;
    }
    at = k == 'R' ? cur_row : cur_col;
    if (del) {
        if (k == 'R')
            snprintf(m, sizeof m, "Delete row %d: Hit ENTER to confirm", cur_row + 1);
        else
            snprintf(m, sizeof m, "Delete column %s: Hit ENTER to confirm",
                     col_name(cur_col, name));
        if (ask_key(m) != KEY_ENTER)
            return;
    }
    rc = del ? sheet_delete(k, at, &err) : sheet_insert(k, at, &err);
    if (rc < 0) {
        snprintf(m, sizeof m, "Error: %s", err);
        set_msg(m);
        return;
    }
    sheet_recalc();
}

/* The slash-command tree. Returns 1 to quit. */
static int cmd_slash(void)
{
    int k = ask_key("Command: B C D E F G I M P R S T V W -");

    switch (k) {
    case KEY_ESC:
    case KEY_BACKSPACE:
    case KEY_CTRL('c'):
        break;
    case 'B':
        if (ask_key("Blank: Hit ENTER to blank this cell") == KEY_ENTER) {
            sheet_blank(cur_row, cur_col);
            sheet_recalc();
        }
        break;
    case 'C':
        if (ask_key("Clear: Type Y to confirm") == 'Y') {
            sheet_clear();
            cur_row = cur_col = top_row = left_col = 0;
        }
        break;
    case 'D':
        cmd_rowcol(1);
        break;
    case 'F':
        cmd_format(0);
        break;
    case 'G':
        cmd_global();
        break;
    case 'I':
        cmd_rowcol(0);
        break;
    case 'S':
        k = ask_key("Storage: L (load) S (save) Q (quit)");
        if (k == 'L')
            cmd_load();
        else if (k == 'S')
            cmd_save();
        else if (k == 'Q')
            return ask_key(sheet_dirty ? "Quit: changes not saved. Type Y to quit anyway"
                                       : "Quit: Type Y to confirm") == 'Y';
        else if (!is_cancel(k))
            set_msg("Not implemented yet");
        break;
    case 'V':
        set_msg("Kalkylark " KALK_VERSION
                "  (c) Set Lonnert and the Kalkylark authors");
        break;
    default:
        set_msg("Not implemented yet");
        break;
    }
    return 0;
}

void ui_run(const char *path)
{
    int k, dr, dc, quit = 0;

    set_msg("Type to enter   > go to   / commands   ! recalc   /SQ quit");
    if (path) {
        snprintf(filename, sizeof filename, "%s", path);
        if (access(filename, F_OK) == 0) {
            load_file();
        } else {
            set_msg("New file: /SS saves it");
            sheet_dirty = 0;
        }
    }
    while (!quit) {
        draw();
        k = term_getkey();
        if (k != KEY_RESIZE)
            set_msg("");

        if (arrow(k, &dr, &dc)) {
            move_by(dr, dc);
            continue;
        }
        switch (k) {
        case KEY_PGUP:  move_by(-visible_rows(), 0); break;
        case KEY_PGDN:  move_by(visible_rows(), 0);  break;
        case KEY_HOME:  cur_row = cur_col = 0; break;
        case '>':       cmd_goto(); break;
        case '/':       quit = cmd_slash(); break;
        case '!':       sheet_recalc(); break;
        case KEY_RESIZE:
        case KEY_CTRL('l'):
            theme_use(ROLE_NORMAL);
            term_clear();
            break;
        case KEY_CTRL('c'):
        case KEY_EOF:
            quit = 1;
            break;
        default:
            if (k >= 32 && k < 127)
                cell_entry(k);
            break;
        }
    }
}
