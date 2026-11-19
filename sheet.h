/*
 * sheet.h - cells, coordinates and recalculation.
 */
#ifndef KALK_SHEET_H
#define KALK_SHEET_H

#include <stddef.h>

#define SHEET_COLS 63       /* A .. BK, as in the original */
#define SHEET_ROWS 254

enum val_kind { VAL_NUM, VAL_NA, VAL_ERR };

struct val {
    enum val_kind kind;
    double num;             /* meaningful when kind == VAL_NUM */
};

enum cell_type { CELL_EMPTY, CELL_LABEL, CELL_VALUE };

struct cell {
    enum cell_type type;
    char fmt;               /* display format, 0 = global (see below) */
    char *text;             /* label text, or the formula as entered */
    struct val v;           /* last computed value */
};

/*
 * Display formats, as in VisiCalc's /F:
 *   G general   I integer   $ two decimals   L left   R right   * bar graph
 * A cell with fmt 0 ("D", default) uses the global format (/GF).
 */
#define FORMATS "GI$LR*"
int  sheet_valid_format(int f);
void sheet_set_format(int row, int col, int fmt);

extern int sheet_colw;      /* global column width (/GC), VisiCalc style */
extern int sheet_gfmt;      /* global format (/GF) */
extern int sheet_dirty;     /* changed since last save/load */
#define COLW_MIN 3
#define COLW_MAX 40

/* Column index -> "A".."BK". out must hold at least 3 chars + NUL. */
const char *col_name(int col, char *out);

/* Parse "B12" (case-insensitive) into 0-based row/col. Returns 0 on success. */
int parse_coord(const char *s, int *row, int *col);

const struct cell *sheet_cell(int row, int col);

/* Does a typed entry starting with ch become a value (else a label)? */
int sheet_is_value_start(int ch);

/* Store an entry. A leading '"' forces a label. On a syntax error returns
 * -1 and sets *msg and *pos (offset into input); the cell is unchanged. */
int sheet_enter(int row, int col, const char *input, const char **msg, int *pos);

void sheet_blank(int row, int col);
void sheet_clear(void);

/* Recalculate every formula.
 *   'C', 'R'  one pass in column or row order, exactly like VisiCalc:
 *             forward references may need another pass ('!').
 *   'N'       natural order, like Lotus 1-2-3: precedents first, so one
 *             recalc is always enough unless there is a circular reference,
 *             which sets sheet_circular. */
extern int sheet_order;
extern int sheet_circular;
void sheet_recalc(void);

/* Text to show for a cell in a column of the given width;
 * *right is set when it should be right-aligned. */
void cell_display(const struct cell *c, int width, char *out, size_t size, int *right);

/* Save to / load from a Kalkylark text file. Return 0, or -1 with a
 * message in err. Loading replaces the whole sheet. */
int sheet_save(const char *path, char *err, size_t errsize);
int sheet_load(const char *path, char *err, size_t errsize);

/* General number format: the most precise form that fits in width. */
void format_number(double v, int width, char *out, size_t size);

#endif
