# Kalkylark - POSIX make
#
#   make            build ./kalkylark
#   make check      unit tests + load every example sheet
#   make docs       screenshots + PDFs in docs/build (needs tectonic, python3)
#   make install    PREFIX=/usr/local by default

CC       = cc
CFLAGS   = -std=c99 -pedantic -Wall -Wextra -O2
LDLIBS   = -lm
PREFIX   = /usr/local
TECTONIC = tectonic
PYTHON   = python3

CORE   = sheet.o formula.o storage.o
OBJS   = main.o term.o theme.o ui.o $(CORE)
DOCS   = tutorial history internals refcard

kalkylark: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

test_core: test_core.o $(CORE)
	$(CC) $(CFLAGS) -o $@ test_core.o $(CORE) $(LDFLAGS) $(LDLIBS)

check: test_core
	./test_core examples/*.kalk

screens: kalkylark
	$(PYTHON) -I tools/screens.py

docs: screens
	mkdir -p docs/build
	for d in $(DOCS); do \
	    $(TECTONIC) -Z search-path=docs/common -Z search-path=docs/screens \
	        -o docs/build docs/$$d/$$d.tex || exit 1; \
	done

install: kalkylark
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/man/man1
	cp kalkylark $(DESTDIR)$(PREFIX)/bin/
	cp kalkylark.1 $(DESTDIR)$(PREFIX)/share/man/man1/

main.o:      main.c term.h theme.h ui.h
term.o:      term.c term.h
theme.o:     theme.c theme.h term.h
sheet.o:     sheet.c sheet.h formula.h
formula.o:   formula.c formula.h sheet.h
storage.o:   storage.c sheet.h
ui.o:        ui.c ui.h sheet.h term.h theme.h
test_core.o: test_core.c sheet.h formula.h

clean:
	rm -f kalkylark test_core $(OBJS) test_core.o
	rm -rf docs/build

.PHONY: check screens docs install clean
