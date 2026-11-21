/*
 * sheet.c - cell storage, entry classification, recalculation, display.
 */
#define _POSIX_C_SOURCE 200809L

#include "sheet.h"
#include "formula.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct cell cells[SHEET_ROWS][SHEET_COLS];
int sheet_order = 'C';
int sheet_colw = 9;
int sheet_gfmt = 'G';
int sheet_dirty;

/* ---- coordinates ----------------------------------------------------- */

const char *col_name(int col, char *out)
{
    if (col < 26) {
        out[0] = (char)('A' + col);
        out[1] = '\0';
    } else {
        out[0] = (char)('A' + col / 26 - 1);
        out[1] = (char)('A' + col % 26);
        out[2] = '\0';
    }
    return out;
}

int parse_coord(const char *s, int *row, int *col)
{
    int c = 0, r = 0, letters = 0, digits = 0;

    while (*s == ' ')
        s++;
    while (isalpha((unsigned char)*s) && letters < 3) {
        c = c * 26 + (toupper((unsigned char)*s) - 'A' + 1);
        s++;
        letters++;
    }
    while (isdigit((unsigned char)*s) && digits < 4) {
        r = r * 10 + (*s - '0');
        s++;
        digits++;
    }
    while (*s == ' ')
        s++;
    if (*s != '\0' || letters == 0 || digits == 0)
        return -1;
    if (c < 1 || c > SHEET_COLS || r < 1 || r > SHEET_ROWS)
        return -1;
    *col = c - 1;
    *row = r - 1;
    return 0;
}

/* ---- cells ----------------------------------------------------------- */

const struct cell *sheet_cell(int row, int col)
{
    return &cells[row][col];
}

int sheet_is_value_start(int ch)
{
    return ch != '\0' && (isdigit(ch) || strchr("+-.(@", ch) != NULL);
}

int sheet_valid_format(int f)
{
    return f != '\0' && strchr(FORMATS, f) != NULL;
}

void sheet_set_format(int row, int col, int fmt)
{
    cells[row][col].fmt = (char)(sheet_valid_format(fmt) ? fmt : 0);
    sheet_dirty = 1;
}

/* Replace contents; the cell keeps its format, as in VisiCalc. */
static void set_cell(struct cell *c, enum cell_type type, char *text)
{
    free(c->text);
    c->type = type;
    c->text = text;
    c->v.kind = VAL_NUM;
    c->v.num = 0;
    sheet_dirty = 1;
}

int sheet_enter(int row, int col, const char *input, const char **msg, int *pos)
{
    char *text;
    size_t i;

    if (input[0] == '"' || !sheet_is_value_start((unsigned char)input[0])) {
        text = strdup(input[0] == '"' ? input + 1 : input);
        if (text == NULL) {
            *msg = "out of memory";
            *pos = 0;
            return -1;
        }
        set_cell(&cells[row][col], CELL_LABEL, text);
        return 0;
    }

    text = strdup(input);
    if (text == NULL) {
        *msg = "out of memory";
        *pos = 0;
        return -1;
    }
    for (i = 0; text[i]; i++)
        text[i] = (char)toupper((unsigned char)text[i]);
    if (formula_check(text, msg, pos) < 0) {
        free(text);
        return -1;
    }
    set_cell(&cells[row][col], CELL_VALUE, text);
    cells[row][col].v = formula_eval(text);
    return 0;
}

void sheet_blank(int row, int col)
{
    set_cell(&cells[row][col], CELL_EMPTY, NULL);
    cells[row][col].fmt = 0;
}

/* Back to a fresh sheet, including global settings. */
void sheet_clear(void)
{
    int r, c;

    for (r = 0; r < SHEET_ROWS; r++)
        for (c = 0; c < SHEET_COLS; c++)
            sheet_blank(r, c);
    sheet_order = 'C';
    sheet_colw = 9;
    sheet_gfmt = 'G';
    sheet_dirty = 1;
}

/* ---- inserting and deleting rows and columns ------------------------- */

static int is_row(int axis) { return axis == 'R'; }

/* Is row or column i free of contents? */
static int line_empty(int axis, int i)
{
    int k, n = is_row(axis) ? SHEET_COLS : SHEET_ROWS;

    for (k = 0; k < n; k++)
        if ((is_row(axis) ? cells[i][k] : cells[k][i]).type != CELL_EMPTY)
            return 0;
    return 1;
}

/* Rewrite every formula for the change, all or nothing: if memory runs
 * out, no formula is touched. */
static int shift_formulas(int axis, int at, int n)
{
    static char *shifted[SHEET_ROWS][SHEET_COLS];
    int r, c, ok = 1;

    for (r = 0; r < SHEET_ROWS; r++)
        for (c = 0; c < SHEET_COLS; c++) {
            shifted[r][c] = NULL;
            if (ok && cells[r][c].type == CELL_VALUE
                && (shifted[r][c] = formula_shift(cells[r][c].text, axis, at, n)) == NULL)
                ok = 0;
        }
    for (r = 0; r < SHEET_ROWS; r++)
        for (c = 0; c < SHEET_COLS; c++) {
            if (shifted[r][c] == NULL)
                continue;
            if (ok) {
                free(cells[r][c].text);
                cells[r][c].text = shifted[r][c];
            } else {
                free(shifted[r][c]);
            }
        }
    return ok ? 0 : -1;
}

int sheet_insert(int axis, int at, const char **msg)
{
    int r;

    if (!line_empty(axis, is_row(axis) ? SHEET_ROWS - 1 : SHEET_COLS - 1)) {
        *msg = is_row(axis) ? "the last row (254) is not empty"
                            : "the last column (BK) is not empty";
        return -1;
    }
    if (shift_formulas(axis, at, 1) < 0) {
        *msg = "out of memory";
        return -1;
    }
    if (is_row(axis)) {
        memmove(&cells[at + 1], &cells[at],
                (size_t)(SHEET_ROWS - 1 - at) * sizeof cells[0]);
        memset(&cells[at], 0, sizeof cells[0]);
    } else {
        for (r = 0; r < SHEET_ROWS; r++) {
            memmove(&cells[r][at + 1], &cells[r][at],
                    (size_t)(SHEET_COLS - 1 - at) * sizeof cells[r][0]);
            memset(&cells[r][at], 0, sizeof cells[r][0]);
        }
    }
    sheet_dirty = 1;
    return 0;
}

int sheet_delete(int axis, int at, const char **msg)
{
    int r, c;

    if (shift_formulas(axis, at, -1) < 0) {
        *msg = "out of memory";
        return -1;
    }
    if (is_row(axis)) {
        for (c = 0; c < SHEET_COLS; c++)
            free(cells[at][c].text);
        memmove(&cells[at], &cells[at + 1],
                (size_t)(SHEET_ROWS - 1 - at) * sizeof cells[0]);
        memset(&cells[SHEET_ROWS - 1], 0, sizeof cells[0]);
    } else {
        for (r = 0; r < SHEET_ROWS; r++) {
            free(cells[r][at].text);
            memmove(&cells[r][at], &cells[r][at + 1],
                    (size_t)(SHEET_COLS - 1 - at) * sizeof cells[r][0]);
            memset(&cells[r][SHEET_COLS - 1], 0, sizeof cells[r][0]);
        }
    }
    sheet_dirty = 1;
    return 0;
}

/* ---- recalculation -------------------------------------------------- */

static void recalc_cell(int r, int c)
{
    if (cells[r][c].type == CELL_VALUE)
        cells[r][c].v = formula_eval(cells[r][c].text);
}

/* ---- natural order --------------------------------------------------- */

#define NCELLS (SHEET_ROWS * SHEET_COLS)
#define IDX(r, c) ((r) * SHEET_COLS + (c))

struct edges {
    int *from, *to;         /* precedent -> dependent, both formula cells */
    size_t n, cap;
    int cur;                /* the dependent being scanned */
    int oom;
};

static void add_edge(int r, int c, void *ctx)
{
    struct edges *e = ctx;
    int *nf, *nt;
    size_t ncap;

    if (cells[r][c].type != CELL_VALUE || e->oom)
        return;
    if (e->n == e->cap) {
        ncap = e->cap ? e->cap * 2 : 1024;
        nf = realloc(e->from, ncap * sizeof *nf);
        if (nf) e->from = nf;
        nt = realloc(e->to, ncap * sizeof *nt);
        if (nt) e->to = nt;
        if (!nf || !nt) {
            e->oom = 1;
            return;
        }
        e->cap = ncap;
    }
    e->from[e->n] = IDX(r, c);
    e->to[e->n] = e->cur;
    e->n++;
}

int sheet_circular;

/*
 * Lotus 1-2-3 style: every formula is computed after the formulas it reads.
 * Kahn's algorithm, so a long chain can't overflow the stack: start with
 * formulas that read no other formula, and release each dependent once all
 * its precedents are done. Whatever is left is on (or downstream of) a
 * cycle: compute it in column order and raise the CIRC flag.
 * Returns -1 if out of memory (caller falls back to column order).
 */
static int recalc_natural(void)
{
    static int indeg[NCELLS], first[NCELLS + 1], queue[NCELLS];
    static unsigned char done[NCELLS];
    struct edges e;
    int *adj = NULL, r, c, x, head = 0, tail = 0;
    size_t i;

    memset(&e, 0, sizeof e);
    memset(indeg, 0, sizeof indeg);
    memset(first, 0, sizeof first);
    memset(done, 0, sizeof done);

    /* 1. edges: who reads whom */
    for (c = 0; c < SHEET_COLS; c++)
        for (r = 0; r < SHEET_ROWS; r++)
            if (cells[r][c].type == CELL_VALUE) {
                e.cur = IDX(r, c);
                formula_refs(cells[r][c].text, add_edge, &e);
            }
    if (e.oom)
        goto fail;

    /* 2. dependents of each cell, as one array (compressed rows) */
    for (i = 0; i < e.n; i++) {
        first[e.from[i] + 1]++;
        indeg[e.to[i]]++;
    }
    for (x = 0; x < NCELLS; x++)
        first[x + 1] += first[x];
    if (e.n > 0 && (adj = malloc(e.n * sizeof *adj)) == NULL)
        goto fail;
    {
        static int fill[NCELLS];
        memcpy(fill, first, sizeof fill);
        for (i = 0; i < e.n; i++)
            adj[fill[e.from[i]]++] = e.to[i];
    }

    /* 3. evaluate in dependency order */
    for (c = 0; c < SHEET_COLS; c++)
        for (r = 0; r < SHEET_ROWS; r++)
            if (cells[r][c].type == CELL_VALUE && indeg[IDX(r, c)] == 0)
                queue[tail++] = IDX(r, c);
    while (head < tail) {
        x = queue[head++];
        cells[x / SHEET_COLS][x % SHEET_COLS].v =
            formula_eval(cells[x / SHEET_COLS][x % SHEET_COLS].text);
        done[x] = 1;
        for (r = first[x]; r < first[x + 1]; r++)
            if (--indeg[adj[r]] == 0)
                queue[tail++] = adj[r];
    }

    /* 4. cycles: one VisiCalc-style pass over what's left */
    sheet_circular = 0;
    for (c = 0; c < SHEET_COLS; c++)
        for (r = 0; r < SHEET_ROWS; r++)
            if (cells[r][c].type == CELL_VALUE && !done[IDX(r, c)]) {
                cells[r][c].v = formula_eval(cells[r][c].text);
                sheet_circular = 1;
            }

    free(adj);
    free(e.from);
    free(e.to);
    return 0;

fail:
    free(adj);
    free(e.from);
    free(e.to);
    return -1;
}

void sheet_recalc(void)
{
    int r, c;

    if (sheet_order == 'N' && recalc_natural() == 0)
        return;
    sheet_circular = 0;     /* row/column order can't tell */
    if (sheet_order == 'R') {
        for (r = 0; r < SHEET_ROWS; r++)
            for (c = 0; c < SHEET_COLS; c++)
                recalc_cell(r, c);
    } else {
        for (c = 0; c < SHEET_COLS; c++)
            for (r = 0; r < SHEET_ROWS; r++)
                recalc_cell(r, c);
    }
}

/* ---- display --------------------------------------------------------- */

/* Strip trailing zeros (and a trailing '.') from a fixed-point string. */
static void trim_zeros(char *s)
{
    char *e;

    if (strchr(s, '.') == NULL)
        return;
    e = s + strlen(s) - 1;
    while (*e == '0')
        *e-- = '\0';
    if (*e == '.')
        *e = '\0';
}

/* "1.50000e+07" -> "1.5e7" */
static void tidy_exponent(char *s)
{
    char mant[64], *e = strchr(s, 'e');
    int ex;

    if (e == NULL)
        return;
    ex = atoi(e + 1);
    *e = '\0';
    snprintf(mant, sizeof mant, "%s", s);
    trim_zeros(mant);
    sprintf(s, "%se%d", mant, ex);
}

void format_number(double v, int width, char *out, size_t size)
{
    char b[96];
    int d;

    if (v == 0)
        v = 0;                  /* no "-0" */
    if (width < 1) {
        out[0] = '\0';
        return;
    }

    /* fixed point, as many decimals as fit; never round a non-zero to 0 */
    for (d = 10; d >= 0; d--) {
        snprintf(b, sizeof b, "%.*f", d, v);
        trim_zeros(b);
        if ((int)strlen(b) <= width) {
            if (v != 0 && (strcmp(b, "0") == 0 || strcmp(b, "-0") == 0))
                break;
            snprintf(out, size, "%s", b);
            return;
        }
    }

    /* scientific */
    for (d = 8; d >= 0; d--) {
        snprintf(b, sizeof b, "%.*e", d, v);
        tidy_exponent(b);
        if ((int)strlen(b) <= width) {
            snprintf(out, size, "%s", b);
            return;
        }
    }

    /* doesn't fit at all */
    if ((size_t)width >= size)
        width = (int)size - 1;
    memset(out, '>', (size_t)width);
    out[width] = '\0';
}

static void overflow(int width, char *out, size_t size)
{
    if (width < 0)
        width = 0;
    if ((size_t)width >= size)
        width = (int)size - 1;
    memset(out, '>', (size_t)width);
    out[width] = '\0';
}

static void format_value(double v, int fmt, int width, char *out, size_t size)
{
    int n;

    switch (fmt) {
    case 'I':
        n = snprintf(out, size, "%.0f", v == 0 ? 0.0 : v);
        if (strcmp(out, "-0") == 0)
            n = snprintf(out, size, "0");
        if (n > width)
            overflow(width, out, size);
        break;
    case '$':
        n = snprintf(out, size, "%.2f", v);
        if (strcmp(out, "-0.00") == 0)
            n = snprintf(out, size, "0.00");
        if (n > width)
            overflow(width, out, size);
        break;
    case '*':                   /* bar graph: one star per unit */
        n = v < 0 ? 0 : v > width ? width : (int)v;
        if ((size_t)n >= size)
            n = (int)size - 1;
        memset(out, '*', (size_t)n);
        out[n] = '\0';
        break;
    default:                    /* G, L, R */
        format_number(v, width, out, size);
        break;
    }
}

void cell_display(const struct cell *c, int width, char *out, size_t size, int *right)
{
    int fmt = c->fmt ? c->fmt : sheet_gfmt;

    *right = 0;
    switch (c->type) {
    case CELL_EMPTY:
        out[0] = '\0';
        break;
    case CELL_LABEL:
        snprintf(out, size, "%s", c->text);
        *right = fmt == 'R';
        break;
    case CELL_VALUE:
        *right = fmt != 'L' && fmt != '*';
        if (c->v.kind == VAL_ERR)
            snprintf(out, size, "ERROR");
        else if (c->v.kind == VAL_NA)
            snprintf(out, size, "NA");
        else
            format_value(c->v.num, fmt, width, out, size);
        break;
    }
}
