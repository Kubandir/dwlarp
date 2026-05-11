PREFIX ?= /usr/local
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter

# ncurses-config covers the unusual layouts (Void: ncursesw6-config; Debian:
# ncursesw5-config or pkg-config ncursesw). Fall back to -lncurses if none
# of those are present.
NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null || \
                          pkg-config --cflags ncurses 2>/dev/null)
NCURSES_LIBS   := $(shell pkg-config --libs ncursesw 2>/dev/null || \
                          pkg-config --libs ncurses 2>/dev/null || \
                          echo -lncurses)

CFLAGS  += $(NCURSES_CFLAGS) -I..
LDLIBS  += $(NCURSES_LIBS)

all: mullvad-menu

mullvad-menu: mullvad-menu.o
	$(CC) $(CFLAGS) -o $@ mullvad-menu.o $(LDLIBS)

mullvad-menu.o: mullvad-menu.c ../config.h
	$(CC) $(CFLAGS) -c -o $@ mullvad-menu.c

install: mullvad-menu
	install -Dm755 mullvad-menu $(DESTDIR)$(PREFIX)/bin/mullvad-menu

clean:
	rm -f mullvad-menu mullvad-menu.o

.PHONY: all install clean
