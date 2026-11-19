/*
 * test_core.c - tests for the formula engine and number formatting.
 * Run with: make check
 */
#define _POSIX_C_SOURCE 200809L

#include "formula.h"
#include "sheet.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures, total;

static void put(const char *coord, const char *text)
{
    const char *msg;
    int r, c, pos;

    if (parse_coord(coord, &r, &c) != 0 || sheet_enter(r, c, text, &msg, &pos) != 0) {
        printf("FAIL  setup %s = %s\n", coord, text);
        failures++;
    }
}

static const struct cell *at(const char *coord)
{
    int r, c;
    parse_coord(coord, &r, &c);
    return sheet_cell(r, c);
}

static void num(const char *f, double want)
{
    struct val v = formula_eval(f);
    total++;
    if (v.kind != VAL_NUM || fabs(v.num - want) > 1e-9 * (fabs(want) + 1)) {
        printf("FAIL  %-28s want %g, got %s %g\n", f, want,
               v.kind == VAL_NUM ? "" : v.kind == VAL_NA ? "NA" : "ERROR", v.num);
        failures++;
    }
}

static void kind(const char *f, enum val_kind want)
{
    struct val v = formula_eval(f);
    total++;
    if (v.kind != want) {
        printf("FAIL  %-28s want kind %d, got %d\n", f, want, v.kind);
        failures++;
    }
}

static void syntax_error(const char *f)
{
    const char *msg;
    int pos;
    total++;
    if (formula_check(f, &msg, &pos) == 0) {
        printf("FAIL  %-28s should be a syntax error\n", f);
        failures++;
    }
}

static void fmt(double v, int width, const char *want)
{
    char out[64];
    format_number(v, width, out, sizeof out);
    total++;
    if (strcmp(out, want) != 0) {
        printf("FAIL  format(%g, %d) want \"%s\", got \"%s\"\n", v, width, want, out);
        failures++;
    }
}

/* Set a cell's format (0 = leave as is) and compare what it displays. */
static void shown(const char *coord, int fmt, const char *want, int want_right)
{
    char out[64];
    int r, c, right;

    parse_coord(coord, &r, &c);
    if (fmt)
        sheet_set_format(r, c, fmt);
    cell_display(sheet_cell(r, c), 8, out, sizeof out, &right);
    total++;
    if (strcmp(out, want) != 0 || right != want_right) {
        printf("FAIL  %s /F%c shows \"%s\"%s, want \"%s\"%s\n", coord, fmt ? fmt : 'D',
               out, right ? " (right)" : "", want, want_right ? " (right)" : "");
        failures++;
    }
}

static void check(int cond, const char *what)
{
    total++;
    if (!cond) {
        printf("FAIL  %s\n", what);
        failures++;
    }
}

static void roundtrip(void)
{
    char path[] = "/tmp/kalkylark-test-XXXXXX", err[256];
    const struct cell *c;
    FILE *f;
    int fd;

    fd = mkstemp(path);
    check(fd >= 0, "mkstemp");
    if (fd < 0)
        return;
    close(fd);

    sheet_clear();
    put("A1", "\"Price: \"quoted\"");     /* colon and quotes in a label */
    put("B1", "19.95");
    sheet_set_format(0, 1, '$');
    put("B2", "+B1*2");
    put("BK254", "@SUM(B1...B2)");
    sheet_set_format(5, 5, 'R');            /* format on an empty cell */
    sheet_colw = 12;
    sheet_gfmt = 'I';
    sheet_order = 'R';
    sheet_recalc();
    check(sheet_save(path, err, sizeof err) == 0, "save");
    check(sheet_dirty == 0, "save clears dirty flag");

    sheet_clear();
    check(sheet_load(path, err, sizeof err) == 0, "load");
    c = at("A1");
    check(c->type == CELL_LABEL && strcmp(c->text, "Price: \"quoted\"") == 0, "label survives");
    check(at("B1")->fmt == '$' && at("B1")->v.num == 19.95, "B1 value and format");
    check(at("B2")->type == CELL_VALUE && strcmp(at("B2")->text, "+B1*2") == 0, "B2 formula");
    check(fabs(at("BK254")->v.num - 59.85) < 1e-9, "BK254 recalculated after load");
    check(sheet_cell(5, 5)->fmt == 'R' && sheet_cell(5, 5)->type == CELL_EMPTY, "format-only cell");
    check(sheet_colw == 12 && sheet_gfmt == 'I' && sheet_order == 'R', "globals");
    check(sheet_dirty == 0, "load clears dirty flag");

    /* a broken file is reported with its line number */
    f = fopen(path, "w");
    fputs("# kalkylark 1\n>A1:10\n>A2:1+\n", f);
    fclose(f);
    check(sheet_load(path, err, sizeof err) != 0 && strncmp(err, "line 3:", 7) == 0,
          "error reports line number");
    check(sheet_load("/nonexistent/x.kalk", err, sizeof err) != 0, "missing file fails");
    remove(path);
}

static double numat(const char *coord)
{
    return at(coord)->v.num;
}

static void natural_order(void)
{
    char coord[8], text[16];
    int r;

    /* forward references: entered so that A1 and A2 are stale */
    sheet_clear();
    put("A1", "+A2*2");
    put("A2", "+A3+1");
    put("A3", "5");
    sheet_order = 'C';
    sheet_recalc();
    check(numat("A1") == 2, "column order: A1 still stale after one pass");
    sheet_order = 'N';
    put("A3", "5");                 /* re-enter to reset */
    sheet_blank(0, 0); put("A1", "+A2*2");
    sheet_recalc();
    check(numat("A1") == 12 && numat("A2") == 6, "natural: one recalc is enough");
    check(sheet_circular == 0, "natural: no false CIRC");

    /* dependencies that run against column order */
    sheet_clear();
    sheet_order = 'N';
    put("A1", "+C9");
    put("C9", "+B5*10");
    put("B5", "+A20");
    put("A20", "3");
    sheet_recalc();
    check(numat("A1") == 30, "natural: zig-zag chain");

    /* ranges and @LOOKUP pick up their dependencies */
    sheet_clear();
    sheet_order = 'N';
    put("A1", "@SUM(B1...B3)");
    put("B1", "+C1"); put("B2", "+C2"); put("B3", "+C3");
    put("C1", "1"); put("C2", "2"); put("C3", "3");
    put("A2", "@LOOKUP(2,D1...D2)");
    put("D1", "1"); put("E1", "+F1");
    put("D2", "2"); put("E2", "+F2");
    put("F1", "10"); put("F2", "20");
    sheet_recalc();
    check(numat("A1") == 6, "natural: range dependencies");
    check(numat("A2") == 20, "natural: @LOOKUP result column is a dependency");

    /* circular references: flagged, still computed, no hang */
    sheet_clear();
    sheet_order = 'N';
    put("A1", "+B1+1");
    put("B1", "+A1");
    put("C1", "+B1*2");             /* downstream of the cycle */
    put("D1", "7");
    put("E1", "+D1");               /* unrelated, must be exact */
    sheet_recalc();
    check(sheet_circular == 1, "natural: cycle sets CIRC");
    check(numat("E1") == 7, "natural: cells outside the cycle are fine");
    sheet_clear();
    sheet_order = 'N';
    put("A1", "+A1+1");
    sheet_recalc();
    check(sheet_circular == 1, "natural: self-reference sets CIRC");
    sheet_blank(0, 0);
    sheet_recalc();
    check(sheet_circular == 0, "natural: CIRC clears when the cycle goes");

    /* the longest possible chain: every cell, column by column, reads
     * the next one; computed in one recalc without recursion */
    sheet_clear();
    sheet_order = 'N';
    for (r = 0; r < SHEET_ROWS * SHEET_COLS - 1; r++) {
        int row = r % SHEET_ROWS, col = r / SHEET_ROWS;
        int nrow = (r + 1) % SHEET_ROWS, ncol = (r + 1) / SHEET_ROWS;
        char a[4], b[4];
        snprintf(coord, sizeof coord, "%s%d", col_name(col, a), row + 1);
        snprintf(text, sizeof text, "+%s%d+1", col_name(ncol, b), nrow + 1);
        put(coord, text);
    }
    put("BK254", "0");
    sheet_recalc();
    check(numat("A1") == SHEET_ROWS * SHEET_COLS - 1, "natural: 16001-cell chain");
    sheet_clear();
}

/* Each sheet named on the command line must load, and no cell may show
 * ERROR after recalculation (used by `make check` for examples/). */
static void check_example(const char *path)
{
    char err[256], what[512];
    int r, c, errors = 0;

    total++;
    if (sheet_load(path, err, sizeof err) != 0) {
        printf("FAIL  %s: %s\n", path, err);
        failures++;
        return;
    }
    sheet_recalc();
    for (r = 0; r < SHEET_ROWS; r++)
        for (c = 0; c < SHEET_COLS; c++)
            if (sheet_cell(r, c)->type == CELL_VALUE
                && sheet_cell(r, c)->v.kind == VAL_ERR)
                errors++;
    if (errors) {
        snprintf(what, sizeof what, "%s: %d cell(s) show ERROR", path, errors);
        printf("FAIL  %s\n", what);
        failures++;
    }
}

int main(int argc, char **argv)
{
    int i;

    /* arithmetic: strictly left to right, like VisiCalc */
    num("1+2", 3);
    num("2+3*4", 20);
    num("2+(3*4)", 14);
    num("-2^2", 4);
    num("10/4", 2.5);
    num("2^10", 1024);
    num("1.5E3", 1500);
    num(".5+.25", 0.75);
    num("  7  -  2 ", 5);
    kind("1/0", VAL_ERR);
    kind("@SQRT(-1)", VAL_ERR);
    kind("@LN(0)", VAL_ERR);

    /* references */
    put("A1", "10");
    put("A2", "20");
    put("A3", "30");
    put("B1", "Hello");             /* label */
    put("B2", "\"123");             /* forced label */
    num("+A1+A2", 30);
    num("A3/A1", 3);
    num("+B1+1", 1);                /* labels count as 0 */
    num("+Z99", 0);                 /* empty counts as 0 */
    num("+a1*2", 20);

    /* list functions */
    num("@SUM(A1...A3)", 60);
    num("@SUM(A1.A3)", 60);
    num("@SUM(A1:A3, 5, A1)", 75);
    num("@SUM(A3...A1)", 60);       /* reversed range */
    num("@MIN(A1...A3)", 10);
    num("@MAX(A1...A3)", 30);
    num("@COUNT(A1...B3)", 3);      /* labels and blanks not counted */
    num("@AVERAGE(A1...A3)", 20);
    kind("@AVERAGE(C1...C9)", VAL_ERR);
    num("@SUM()", 0);

    /* constants, NA and ERROR propagation */
    num("@PI", 3.14159265358979323846);
    num("@ABS(-3)", 3);
    num("@INT(-3.7)", -3);
    num("@INT(3.7)", 3);
    kind("@NA", VAL_NA);
    kind("@NA+1", VAL_NA);
    kind("@ERROR*0", VAL_ERR);
    kind("@NA+@ERROR", VAL_ERR);
    put("A4", "@NA");
    kind("@SUM(A1...A4)", VAL_NA);

    /* NPV: 100/(1.1) + 100/(1.1^2) */
    put("C1", "100");
    put("C2", "100");
    num("@NPV(.1,C1...C2)", 100 / 1.1 + 100 / 1.21);

    /* LOOKUP: thresholds in D, results in E */
    put("D1", "0");    put("E1", "1");
    put("D2", "100");  put("E2", "2");
    put("D3", "500");  put("E3", "3");
    num("@LOOKUP(50,D1...D3)", 1);
    num("@LOOKUP(100,D1...D3)", 2);
    num("@LOOKUP(9999,D1...D3)", 3);
    kind("@LOOKUP(-1,D1...D3)", VAL_NA);
    kind("@LOOKUP(1,D1...E3)", VAL_ERR);   /* 2-D range */

    /* recalculation: A1 was computed while A2 was still empty */
    sheet_clear();
    put("A1", "+A2*2");
    put("A2", "5");
    sheet_recalc();
    total++;
    if (at("A1")->v.num != 10) {
        printf("FAIL  recalc A1 = %g, want 10\n", at("A1")->v.num);
        failures++;
    }

    /* syntax errors */
    syntax_error("1+");
    syntax_error("(1+2");
    syntax_error("1+2)");
    syntax_error("@FOO(1)");
    syntax_error("@SUM(A1...)");
    syntax_error("+BL1");
    syntax_error("+A255");
    syntax_error("1 2");
    syntax_error("@NPV(1,2)");

    /* number formatting */
    fmt(0, 8, "0");
    fmt(42, 8, "42");
    fmt(-42, 8, "-42");
    fmt(1.0 / 3, 8, "0.333333");
    fmt(2.5, 8, "2.5");
    fmt(12345678, 8, "12345678");
    fmt(123456789, 8, "1.2346e8");
    fmt(0.0000001, 8, "1e-7");
    fmt(-0.0, 8, "0");
    fmt(1e300, 3, ">>>");

    /* display formats */
    sheet_clear();
    put("A1", "1234.567");
    put("A2", "Name");
    shown("A1", 0,   "1234.567", 1);
    shown("A1", 'I', "1235", 1);
    shown("A1", '$', "1234.57", 1);
    shown("A1", 'L', "1234.567", 0);
    shown("A2", 'R', "Name", 1);
    put("A3", "3.9");
    shown("A3", '*', "***", 0);
    put("A4", "-0.001");
    shown("A4", '$', "0.00", 1);
    shown("A4", 'I', "0", 1);
    put("A5", "123456789");
    shown("A5", '$', ">>>>>>>>", 1);

    /* global format applies when the cell has none */
    sheet_set_format(0, 0, 0);
    sheet_gfmt = '$';
    shown("A1", 0, "1234.57", 1);
    sheet_gfmt = 'G';

    /* save / load round trip */
    roundtrip();

    natural_order();

    for (i = 1; i < argc; i++)
        check_example(argv[i]);

    printf("%d/%d passed\n", total - failures, total);
    return failures != 0;
}
