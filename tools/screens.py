#!/usr/bin/env python3
"""
screens.py - make the documentation's screenshots from the real program.

Each scenario starts ./kalkylark in a pseudo-terminal, types keys, and
renders what the program drew into docs/screens/<name>.tex as LaTeX
(macros from docs/common/kalkylark.sty). Because the screens come from the
program itself, rebuilding the docs keeps them true to the code.

Only the escape sequences Kalkylark emits are understood: cursor position,
erase line/screen and SGR colours in 256-colour mode.

Usage: python3 -I tools/screens.py      (run from the repository root)
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import termios
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "kalkylark")
OUTDIR = os.path.join(ROOT, "docs", "screens")
EXAMPLES = os.path.join(ROOT, "examples")

# 256-colour indices used by the green theme -> intensity
LEVEL = {28: "D", 83: "N", 157: "B"}

UP, DOWN, LEFT, RIGHT = b"\x1b[A", b"\x1b[B", b"\x1b[D", b"\x1b[C"
ENTER, ESC = b"\r", b"\x1b"


class Screen:
    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.chars = [[" "] * cols for _ in range(rows)]
        self.attrs = [[("N", False)] * cols for _ in range(rows)]
        self.y = self.x = 0
        self.attr = ("N", False)
        self.pending = b""

    def feed(self, data):
        # an escape sequence may be split across two reads: keep the tail
        data = self.pending + data
        cut = data.rfind(b"\x1b")
        if cut >= 0 and not re.match(rb"\x1b\[[?0-9;]*[A-Za-z]", data[cut:]):
            data, self.pending = data[:cut], data[cut:]
        else:
            self.pending = b""
        s = data.decode("utf-8", "replace")
        i = 0
        while i < len(s):
            m = re.match(r"\x1b\[([?0-9;]*)([A-Za-z])", s[i:])
            if m:
                self.csi(m.group(1), m.group(2))
                i += len(m.group(0))
                continue
            if s[i] >= " ":
                if 0 <= self.y < self.rows and 0 <= self.x < self.cols:
                    self.chars[self.y][self.x] = s[i]
                    self.attrs[self.y][self.x] = self.attr
                self.x += 1
            i += 1

    def csi(self, p, f):
        if f == "H":
            a = [int(v) if v else 1 for v in p.split(";")] if p else [1, 1]
            self.y, self.x = a[0] - 1, (a[1] - 1 if len(a) > 1 else 0)
        elif f == "J":
            for r in range(self.rows):
                self.chars[r] = [" "] * self.cols
                self.attrs[r] = [self.attr] * self.cols
        elif f == "K":
            for c in range(self.x, self.cols):
                self.chars[self.y][c] = " "
                self.attrs[self.y][c] = self.attr
        elif f == "m":
            codes = [int(v) if v else 0 for v in p.split(";")] if p else [0]
            level, rev = self.attr
            k = 0
            while k < len(codes):
                c = codes[k]
                if c == 0:
                    level, rev = "N", False
                elif c == 7:
                    rev = True
                elif c in (38, 48) and k + 2 < len(codes) and codes[k + 1] == 5:
                    if c == 38:
                        level = LEVEL.get(codes[k + 2], "N")
                    k += 2
                k += 1
            self.attr = (level, rev)

    def latex(self, caption=None):
        out = ["\\begin{kscreen}{%d}" % self.cols]
        for r in range(self.rows):
            runs, cur, buf = [], None, []
            for c in range(self.cols):
                a = self.attrs[r][c]
                if a != cur and buf:
                    runs.append((cur, "".join(buf)))
                    buf = []
                cur = a
                buf.append(self.chars[r][c])
            if buf:
                runs.append((cur, "".join(buf)))
            # trailing plain spaces carry no information
            while runs and not runs[-1][0][1] and runs[-1][1].strip() == "":
                runs.pop()
            line = "".join(
                "\\k%s%s{%s}" % ("R" if rev else "", level, escape(text))
                for (level, rev), text in runs)
            out.append("\\kline{%s}" % line)
        out.append("\\end{kscreen}")
        return "\n".join(out) + "\n"


SPECIAL = {
    "\\": "\\textbackslash{}", "{": "\\{", "}": "\\}", "$": "\\$",
    "&": "\\&", "#": "\\#", "^": "\\textasciicircum{}", "_": "\\_",
    "%": "\\%", "~": "\\textasciitilde{}", " ": "\\ks{}", "-": "-{}",
    ">": "\\textgreater{}", "<": "\\textless{}", '"': "\\textquotedbl{}",
}


def escape(text):
    return "".join(SPECIAL.get(ch, ch) for ch in text)


class Session:
    def __init__(self, args, rows=16, cols=64):
        self.screen = Screen(rows, cols)
        env = dict(os.environ, TERM="xterm-256color")
        env.pop("NO_COLOR", None)
        env.pop("COLORTERM", None)
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(EXAMPLES)
            os.execve(BINARY, [BINARY, "-p", "green", "-c", "256"] + args, env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.pump(0.4)

    def pump(self, t=0.15):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.03)
            if r:
                try:
                    self.screen.feed(os.read(self.fd, 65536))
                except OSError:
                    return

    def keys(self, *seq):
        for k in seq:
            os.write(self.fd, k)
            self.pump()

    def shot(self, name):
        path = os.path.join(OUTDIR, name + ".tex")
        with open(path, "w") as f:
            f.write(self.screen.latex())
        print("  " + os.path.relpath(path, ROOT))

    def close(self):
        """Quit the way a user does, reading output until the program exits
        (it restores the terminal with TCSAFLUSH, which waits for us)."""
        os.write(self.fd, ESC + b"/SQY")
        end = time.time() + 5
        while time.time() < end:
            self.pump(0.05)
            pid, _ = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                break
        else:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        os.close(self.fd)


def typed(text):
    return text.encode()


def scenarios():
    # 1. a fresh sheet
    s = Session([])
    s.shot("empty")
    # typing a label: the prompt line says Label
    s.keys(typed("Rent"))
    s.shot("label")
    s.keys(RIGHT, typed("9500"))
    s.shot("value")
    s.keys(LEFT, DOWN, typed("Food"), RIGHT, typed("4200"), LEFT, DOWN,
           typed("Total"), RIGHT)
    # pointing: @SUM( then up twice, '.', down once
    s.keys(typed("@SUM("), UP, UP)
    s.shot("pointing")
    s.keys(typed("."), DOWN)
    s.shot("range")
    s.keys(typed(")"), ENTER)
    s.shot("sum")
    s.keys(typed("/"))
    s.shot("menu")
    s.keys(ESC, typed("2+3*4"), ENTER)
    s.shot("leftright")
    s.close()

    # 2. the budget example, formatted
    s = Session(["budget.kalk"])
    s.keys(DOWN, DOWN, DOWN, DOWN, DOWN, DOWN, DOWN, RIGHT)
    s.shot("budget")
    s.close()

    # 3. formats side by side
    s = Session([])
    for f in ["G", "I", "$", "L", "R", "*"]:
        s.keys(typed("12.345"), ENTER, typed("/F" + f), RIGHT)
    s.keys(typed("/GC7"), ENTER, typed(">a1"), ENTER)
    s.shot("formats")
    s.close()

    # 4. forward reference: stale in column order, fixed by /GON
    s = Session([])
    s.keys(typed("+A2*2"), DOWN, typed("+A3+1"), DOWN, typed("5"), ENTER)
    s.shot("stale")
    s.keys(typed("/GON"))
    s.shot("natural")
    s.close()

    # 5. a deliberate circular reference
    s = Session(["iterate.kalk"])
    s.keys(DOWN)
    s.shot("circ")
    s.keys(typed("!"), typed("!"), typed("!"), typed("!"), typed("!"))
    s.shot("circ-settled")
    s.close()

    # 6. loan and tax examples
    s = Session(["loan.kalk"])
    s.keys(*([DOWN] * 6), RIGHT)
    s.shot("loan")
    s.close()
    s = Session(["tax.kalk"])
    s.keys(RIGHT, RIGHT, RIGHT, RIGHT, DOWN)
    s.shot("tax")
    s.close()


def main():
    if not os.access(BINARY, os.X_OK):
        sys.exit("screens.py: build ./kalkylark first (make)")
    os.makedirs(OUTDIR, exist_ok=True)
    print("screens:")
    scenarios()


if __name__ == "__main__":
    main()
