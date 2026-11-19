/*
 * formula.c - recursive-descent parser that evaluates as it parses.
 *
 *   expr    := unary { op unary }          op: + - * / ^, left to right
 *   unary   := '-' unary | '+' unary | primary
 *   primary := number | ref | '(' expr ')' | '@' function
 *
 * A referenced cell contributes its last computed value; empty cells and
 * labels count as 0. ERROR beats NA, and both propagate through arithmetic.
 */
#define _POSIX_C_SOURCE 200809L

#include "formula.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct parser {
    const char *s;          /* current position */
    const char *start;
    const char *err;        /* first syntax error, if any */
    const char *err_at;
    formula_ref_fn on_ref;  /* if set: report every cell the formula reads */
    void *ctx;
};

struct range { int r1, c1, r2, c2; };

static void report_range(struct parser *p, int r1, int c1, int r2, int c2)
{
    int r, c;

    if (p->on_ref == NULL)
        return;
    if (r2 >= SHEET_ROWS) r2 = SHEET_ROWS - 1;
    if (c2 >= SHEET_COLS) c2 = SHEET_COLS - 1;
    for (r = r1; r <= r2; r++)
        for (c = c1; c <= c2; c++)
            p->on_ref(r, c, p->ctx);
}

/* ---- values ---------------------------------------------------------- */

static struct val mk_num(double n)
{
    struct val v;
    if (isfinite(n)) {
        v.kind = VAL_NUM;
        v.num = n;
    } else {
        v.kind = VAL_ERR;
        v.num = 0;
    }
    return v;
}

static struct val mk(enum val_kind k)
{
    struct val v;
    v.kind = k;
    v.num = 0;
    return v;
}

/* Non-number operand wins: ERROR over NA. Returns 1 if it decided r. */
static int propagate(struct val a, struct val b, struct val *r)
{
    if (a.kind == VAL_ERR || b.kind == VAL_ERR) {
        *r = mk(VAL_ERR);
        return 1;
    }
    if (a.kind == VAL_NA || b.kind == VAL_NA) {
        *r = mk(VAL_NA);
        return 1;
    }
    return 0;
}

static struct val binop(struct val a, int op, struct val b)
{
    struct val r;

    if (propagate(a, b, &r))
        return r;
    switch (op) {
    case '+': return mk_num(a.num + b.num);
    case '-': return mk_num(a.num - b.num);
    case '*': return mk_num(a.num * b.num);
    case '/': return b.num == 0 ? mk(VAL_ERR) : mk_num(a.num / b.num);
    case '^': return mk_num(pow(a.num, b.num));
    }
    return mk(VAL_ERR);
}

static struct val cell_val(int r, int c)
{
    const struct cell *cl = sheet_cell(r, c);
    return cl->type == CELL_VALUE ? cl->v : mk_num(0);
}

/* ---- lexing helpers -------------------------------------------------- */

static void fail(struct parser *p, const char *msg)
{
    if (p->err == NULL) {
        p->err = msg;
        p->err_at = p->s;
    }
}

static void skip(struct parser *p)
{
    while (*p->s == ' ')
        p->s++;
}

static int accept(struct parser *p, int ch)
{
    skip(p);
    if (*p->s == ch) {
        p->s++;
        return 1;
    }
    return 0;
}

static void expect(struct parser *p, int ch, const char *msg)
{
    if (!accept(p, ch))
        fail(p, msg);
}

/* A cell reference like A1 or BK254. Leaves p unchanged if there is none. */
static int parse_ref(struct parser *p, int *row, int *col)
{
    const char *s = p->s;
    int c = 0, r = 0, nl = 0, nd = 0;

    while (isalpha((unsigned char)*s) && nl < 3) {
        c = c * 26 + (toupper((unsigned char)*s) - 'A' + 1);
        s++;
        nl++;
    }
    while (isdigit((unsigned char)*s) && nd < 4) {
        r = r * 10 + (*s - '0');
        s++;
        nd++;
    }
    if (nl == 0 || nd == 0 || isalnum((unsigned char)*s))
        return 0;
    if (c > SHEET_COLS || r < 1 || r > SHEET_ROWS)
        return 0;
    *row = r - 1;
    *col = c - 1;
    p->s = s;
    return 1;
}

/* ref (...|..|.|:) ref. Leaves p unchanged if this isn't a range. */
static int parse_range(struct parser *p, struct range *rg)
{
    const char *save;
    int dots = 0, t;

    skip(p);
    save = p->s;
    if (!parse_ref(p, &rg->r1, &rg->c1))
        return 0;
    skip(p);
    if (*p->s == ':') {
        p->s++;
    } else {
        while (*p->s == '.' && dots < 3) {
            p->s++;
            dots++;
        }
        if (dots == 0) {
            p->s = save;
            return 0;
        }
    }
    skip(p);
    if (!parse_ref(p, &rg->r2, &rg->c2)) {
        fail(p, "expected a cell after the range dots");
        rg->r2 = rg->r1;
        rg->c2 = rg->c1;
    }
    if (rg->r1 > rg->r2) { t = rg->r1; rg->r1 = rg->r2; rg->r2 = t; }
    if (rg->c1 > rg->c2) { t = rg->c1; rg->c1 = rg->c2; rg->c2 = t; }
    report_range(p, rg->r1, rg->c1, rg->r2, rg->c2);
    return 1;
}

/* ---- grammar --------------------------------------------------------- */

static struct val parse_expr(struct parser *p);

static struct val parse_number(struct parser *p)
{
    const char *s = p->s;
    char buf[64];
    size_t n;
    int digits = 0;

    while (isdigit((unsigned char)*s)) { s++; digits++; }
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) { s++; digits++; }
    }
    if (digits == 0) {
        fail(p, "expected a number");
        return mk(VAL_ERR);
    }
    if ((*s == 'E' || *s == 'e')
        && (isdigit((unsigned char)s[1])
            || ((s[1] == '+' || s[1] == '-') && isdigit((unsigned char)s[2])))) {
        s += 2;
        while (isdigit((unsigned char)*s))
            s++;
    }
    n = (size_t)(s - p->s);
    if (n >= sizeof buf) {
        fail(p, "number too long");
        return mk(VAL_ERR);
    }
    memcpy(buf, p->s, n);
    buf[n] = '\0';
    p->s = s;
    return mk_num(strtod(buf, NULL));
}

/* Running totals for the list functions. */
struct agg {
    double sum, min, max;
    int n, na, err;
};

static void agg_add(struct agg *a, struct val v)
{
    if (v.kind == VAL_ERR) { a->err = 1; return; }
    if (v.kind == VAL_NA)  { a->na = 1;  return; }
    if (a->n == 0 || v.num < a->min) a->min = v.num;
    if (a->n == 0 || v.num > a->max) a->max = v.num;
    a->sum += v.num;
    a->n++;
}

/* (item, item, ...) where each item is a range or an expression.
 * Only value cells in a range take part (blanks and labels are skipped). */
static void parse_list(struct parser *p, struct agg *a)
{
    struct range rg;
    int r, c;

    memset(a, 0, sizeof *a);
    expect(p, '(', "expected '('");
    if (accept(p, ')'))
        return;
    do {
        if (parse_range(p, &rg)) {
            for (r = rg.r1; r <= rg.r2; r++)
                for (c = rg.c1; c <= rg.c2; c++)
                    if (sheet_cell(r, c)->type == CELL_VALUE)
                        agg_add(a, sheet_cell(r, c)->v);
        } else {
            agg_add(a, parse_expr(p));
        }
    } while (accept(p, ',') && p->err == NULL);
    expect(p, ')', "expected ')'");
}

static struct val fn_list(struct parser *p, const char *name)
{
    struct agg a;

    parse_list(p, &a);
    if (a.err) return mk(VAL_ERR);
    if (a.na)  return mk(VAL_NA);
    if (strcmp(name, "SUM") == 0)   return mk_num(a.sum);
    if (strcmp(name, "COUNT") == 0) return mk_num(a.n);
    if (a.n == 0)                   return mk(VAL_ERR);
    if (strcmp(name, "MIN") == 0)   return mk_num(a.min);
    if (strcmp(name, "MAX") == 0)   return mk_num(a.max);
    return mk_num(a.sum / a.n);     /* AVERAGE */
}

/* @NPV(rate, range): sum of v[i] / (1+rate)^i, i = 1.. */
static struct val fn_npv(struct parser *p)
{
    struct val rate, v, r = mk_num(0);
    struct range rg;
    int i = 1, row, col;
    double total = 0;

    expect(p, '(', "expected '('");
    rate = parse_expr(p);
    expect(p, ',', "expected ','");
    if (!parse_range(p, &rg))
        fail(p, "expected a range");
    expect(p, ')', "expected ')'");
    if (p->err)
        return mk(VAL_ERR);

    for (row = rg.r1; row <= rg.r2; row++)
        for (col = rg.c1; col <= rg.c2; col++, i++) {
            v = cell_val(row, col);
            if (propagate(rate, v, &r))
                return r;
            total += v.num / pow(1 + rate.num, i);
        }
    return mk_num(total);
}

/* @LOOKUP(x, range): range is one row or column in ascending order; find
 * the last entry <= x and return the cell beside it (right of a column,
 * below a row). */
static struct val fn_lookup(struct parser *p)
{
    struct val x, v, r;
    struct range rg;
    int vertical, i, n, row, col, hit = -1;

    expect(p, '(', "expected '('");
    x = parse_expr(p);
    expect(p, ',', "expected ','");
    if (!parse_range(p, &rg))
        fail(p, "expected a range");
    expect(p, ')', "expected ')'");
    if (p->err)
        return mk(VAL_ERR);
    if (rg.r1 != rg.r2 && rg.c1 != rg.c2)
        return mk(VAL_ERR);         /* must be a single row or column */

    vertical = rg.c1 == rg.c2;
    /* The result comes from the cells beside the range; which one depends
     * on the data, so report them all (like Excel's dynamic dependencies). */
    if (vertical)
        report_range(p, rg.r1, rg.c1 + 1, rg.r2, rg.c1 + 1);
    else
        report_range(p, rg.r1 + 1, rg.c1, rg.r1 + 1, rg.c2);
    n = vertical ? rg.r2 - rg.r1 + 1 : rg.c2 - rg.c1 + 1;
    for (i = 0; i < n; i++) {
        row = vertical ? rg.r1 + i : rg.r1;
        col = vertical ? rg.c1 : rg.c1 + i;
        v = cell_val(row, col);
        if (propagate(x, v, &r))
            return r;
        if (v.num > x.num)
            break;
        hit = i;
    }
    if (hit < 0)
        return mk(VAL_NA);
    row = vertical ? rg.r1 + hit : rg.r1 + 1;
    col = vertical ? rg.c1 + 1 : rg.c1 + hit;
    if (row >= SHEET_ROWS || col >= SHEET_COLS)
        return mk(VAL_ERR);
    return cell_val(row, col);
}

static double fn_int(double x) { return x < 0 ? ceil(x) : floor(x); }

static const struct {
    const char *name;
    double (*f)(double);
} math1[] = {
    { "ABS", fabs }, { "INT", fn_int }, { "SQRT", sqrt }, { "EXP", exp },
    { "LN", log }, { "LOG10", log10 }, { "SIN", sin }, { "COS", cos },
    { "TAN", tan }, { "ASIN", asin }, { "ACOS", acos }, { "ATAN", atan },
};

static struct val parse_function(struct parser *p)
{
    char name[16];
    size_t n = 0, i;
    struct val x;

    while (isalnum((unsigned char)*p->s)) {
        if (n + 1 < sizeof name)
            name[n++] = (char)toupper((unsigned char)*p->s);
        p->s++;
    }
    name[n] = '\0';

    if (strcmp(name, "PI") == 0)    return mk_num(3.14159265358979323846);
    if (strcmp(name, "NA") == 0)    return mk(VAL_NA);
    if (strcmp(name, "ERROR") == 0) return mk(VAL_ERR);
    if (strcmp(name, "SUM") == 0 || strcmp(name, "MIN") == 0
        || strcmp(name, "MAX") == 0 || strcmp(name, "COUNT") == 0
        || strcmp(name, "AVERAGE") == 0)
        return fn_list(p, name);
    if (strcmp(name, "NPV") == 0)    return fn_npv(p);
    if (strcmp(name, "LOOKUP") == 0) return fn_lookup(p);

    for (i = 0; i < sizeof math1 / sizeof math1[0]; i++) {
        if (strcmp(name, math1[i].name) == 0) {
            expect(p, '(', "expected '('");
            x = parse_expr(p);
            expect(p, ')', "expected ')'");
            if (x.kind != VAL_NUM)
                return x;
            return mk_num(math1[i].f(x.num));
        }
    }
    fail(p, "unknown function");
    return mk(VAL_ERR);
}

static struct val parse_primary(struct parser *p)
{
    struct val v;
    int r, c;

    skip(p);
    if (*p->s == '(') {
        p->s++;
        v = parse_expr(p);
        expect(p, ')', "expected ')'");
        return v;
    }
    if (isdigit((unsigned char)*p->s) || *p->s == '.')
        return parse_number(p);
    if (*p->s == '@') {
        p->s++;
        return parse_function(p);
    }
    if (isalpha((unsigned char)*p->s)) {
        if (parse_ref(p, &r, &c)) {
            report_range(p, r, c, r, c);
            return cell_val(r, c);
        }
        fail(p, "not a cell (A1 .. BK254)");
        return mk(VAL_ERR);
    }
    fail(p, *p->s ? "unexpected character" : "incomplete formula");
    return mk(VAL_ERR);
}

static struct val parse_unary(struct parser *p)
{
    struct val v;

    skip(p);
    if (*p->s == '-') {
        p->s++;
        v = parse_unary(p);
        if (v.kind == VAL_NUM)
            v.num = -v.num;
        return v;
    }
    if (*p->s == '+') {
        p->s++;
        return parse_unary(p);
    }
    return parse_primary(p);
}

static struct val parse_expr(struct parser *p)
{
    struct val a = parse_unary(p);
    int op;

    for (;;) {
        skip(p);
        op = *p->s;
        if (op == '\0' || strchr("+-*/^", op) == NULL || p->err)
            return a;
        p->s++;
        a = binop(a, op, parse_unary(p));
    }
}

static struct val run(const char *s, struct parser *p)
{
    struct val v;

    p->s = p->start = s;
    p->err = p->err_at = NULL;
    if (p->on_ref == NULL)
        p->ctx = NULL;
    v = parse_expr(p);
    skip(p);
    if (*p->s != '\0')
        fail(p, *p->s == ')' ? "unbalanced ')'" : "unexpected character");
    return p->err ? mk(VAL_ERR) : v;
}

int formula_check(const char *s, const char **msg, int *pos)
{
    struct parser p;

    p.on_ref = NULL;
    run(s, &p);
    if (p.err == NULL)
        return 0;
    *msg = p.err;
    *pos = (int)(p.err_at - p.start);
    return -1;
}

struct val formula_eval(const char *s)
{
    struct parser p;

    p.on_ref = NULL;
    return run(s, &p);
}

void formula_refs(const char *s, formula_ref_fn fn, void *ctx)
{
    struct parser p;

    p.on_ref = fn;
    p.ctx = ctx;
    run(s, &p);
}
