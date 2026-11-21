# Kalkylark

A VisiCalc-style spreadsheet for VT100 terminals, written from scratch in
portable C. *Kalkylark* is Swedish for *spreadsheet*. With the adoption of
AI of today, the architecture becomes more important in understanding than
the code itself. The historical traits can even enlighten us to understand
historical constraints, but also solutions that were made under such.

Let's see what the program looks like:
```
 B8 /F$ (V) @SUM(B3...B6)                         N  KALKYLARK


        A          B          C          D          E
  1 Budget     November
  2
  3 Rent          9500.00
  4 Food          4200.00
  5 Transport      970.00
  6 Other         1500.00
  7
  8 Total        16170.00
  9 Per day        539.00
```

- **1979 at heart.** VisiCalc's screen layout, `/` command menus, labels
  and values, `@` functions, building formulas by pointing with the arrow
  keys, and left-to-right formula evaluation (`2+3*4` is 20).
- **And the decade after.** Natural-order recalculation with circular
  reference detection, as in Lotus 1-2-3, as an option next to VisiCalc's
  one-pass column and row order.
- **No dependencies.** C99 and POSIX: `termios` for the keyboard and ANSI
  escape sequences for the screen, with no curses. It builds with `make` on
  Linux, macOS and the BSDs.
- **Phosphor themes.** `-p green`, `-p amber` or `-p white`, for the
  monitors of the time.
- **Plain-text files** that are easy to read, compare and generate.

## Build and run

```sh
make
./kalkylark -p green examples/budget.kalk
make check            # unit tests + every example sheet
make install          # to /usr/local (PREFIX=... to change)
```

Quit with `/SQY`. `man ./kalkylark.1` lists every key, command and function.

## Documentation

| Links | Description |
|---|---|
| [Tutorial](docs/tutorial/tutorial.tex) | from an empty sheet to worked examples, with screens generated from the program |
| [Reference card](docs/refcard/refcard.tex) | every key, command, function and format on one page |
| [Internals](docs/internals/internals.tex) | a walk through the source: terminal layer, parser, recalculation, testing |
| [History](docs/history/history.tex) | how spreadsheets learned to recalculate, from VisiCalc's sweep to dependency graphs and today's "signals", plus the rights situation around VisiCalc |
| [`kalkylark.1`](kalkylark.1) | the man page |

The PDFs are built with [tectonic](https://tectonic-typesetting.github.io):

```sh
make docs             # -> docs/build/*.pdf
```

Readymade [PDFs](./build).

`make docs` first runs `tools/screens.py`, which starts Kalkylark in a
pseudo-terminal, types into it, and turns what it draws into LaTeX. Every
screen in the documents therefore comes from the current code. The PDFs are
also attached to each release.

## Layout

| Path | Contents |
|---|---|
| `term.c` | raw mode, key decoding, buffered VT100 output |
| `theme.c` | phosphor colour themes |
| `formula.c` | formula parser and evaluator |
| `sheet.c` | cells, recalculation (column, row, natural order), number formats |
| `storage.c` | save and load |
| `ui.c` | screen layout, cell entry, pointing, `/` commands |
| `test_core.c` | engine tests (`make check`) |
| `examples/` | example sheets |
| `docs/` | LaTeX sources, shared style, generated screens |
| `tools/screens.py` | screenshot generator |

## Author and licence

Written by Set Lonnert and the Kalkylark authors. The code is under the
[BSD 2-Clause licence](LICENSE). The documentation in
`docs/` is under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

## Not affiliated

Kalkylark is an independent implementation of the *behaviour* of VisiCalc,
written from scratch. It contains no code, artwork or documentation from the
original, and is not affiliated with or endorsed by Lotus Development
Corporation, IBM, Software Arts, VisiCorp, Dan Bricklin or Bob Frankston.
VisiCalc is a trademark of its respective owner.
