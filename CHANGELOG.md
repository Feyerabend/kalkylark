# Changelog

## 0.1 — unreleased

First public version.

- VT100 terminal layer on POSIX `termios`, no curses; phosphor themes
  (`-p green|amber|white`) at 16, 256 or 24-bit colour.
- 63 × 254 sheet, labels and values, VisiCalc's left-to-right formulas,
  pointing with the arrow keys, ranges `A1...A9`.
- Functions: `@SUM @MIN @MAX @COUNT @AVERAGE @NPV @LOOKUP @ABS @INT @SQRT
  @EXP @LN @LOG10 @SIN @COS @TAN @ASIN @ACOS @ATAN @PI @NA @ERROR`.
- Recalculation in column or row order (VisiCalc) or natural order
  (Lotus 1-2-3), with circular-reference detection (`CIRC`).
- Formats `/F` and `/GF` (G I $ L R *), global column width `/GC`.
- Plain-text files: `/SS`, `/SL`, file name on the command line.
- Man page, example sheets, and two LaTeX documents built with tectonic:
  a tutorial and a history of spreadsheet recalculation.
