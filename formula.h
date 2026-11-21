/*
 * formula.h - VisiCalc formula language.
 *
 * Operators + - * / ^ are evaluated strictly left to right with no
 * precedence (2+3*4 is 20), as in the original; use parentheses.
 * References: A1 .. BK254. Ranges: A1...A9 (also A1.A9, A1..A9, A1:A9).
 * Functions: @SUM @MIN @MAX @COUNT @AVERAGE @NPV @LOOKUP @ABS @INT @SQRT
 *            @EXP @LN @LOG10 @SIN @COS @TAN @ASIN @ACOS @ATAN @PI @NA @ERROR
 */
#ifndef KALK_FORMULA_H
#define KALK_FORMULA_H

#include "sheet.h"

/* Syntax check. Returns 0, or -1 with a message and the error offset. */
int formula_check(const char *s, const char **msg, int *pos);

/* Evaluate against the current cell values (no recursion into cells). */
struct val formula_eval(const char *s);

/* Call fn for every cell the formula may read: single references, every
 * cell of a range, and for @LOOKUP the row/column its result comes from.
 * Cells can be reported more than once. */
typedef void (*formula_ref_fn)(int row, int col, void *ctx);
void formula_refs(const char *s, formula_ref_fn fn, void *ctx);

/* The formula rewritten for rows (axis 'R') or columns ('C') inserted or
 * deleted: n > 0 inserts n before index at, n < 0 deletes -n from at on.
 * References past the change move with their cells; a range grows or
 * shrinks; a reference to a deleted cell becomes @ERROR. Returns a new
 * string to free, or NULL if out of memory. */
char *formula_shift(const char *s, int axis, int at, int n);

#endif
