CC      ?= cc
CFLAGS  ?= -Os -Wall -Wextra -Wno-unused-parameter \
           -fno-asynchronous-unwind-tables -fdata-sections -ffunction-sections
LDFLAGS ?= -Wl,--gc-sections -Wl,-s -Wl,--as-needed -lm

PREFIX  ?= $(HOME)/.local

FONT    ?= $(HOME)/.local/share/fonts/NerdFonts/FiraCodeNerdFont-Bold.ttf

SRC := twl.c wl.c widget.c render.c status.c bar.c hud.c menu.c ctl.c osd.c dbus.c wall.c media.c gamma.c lock.c xkb.c
OBJ := $(SRC:%.c=build/%.o)
HDR := twl.h proto.h config.h bake.h
BIN := build/twl build/twlctl build/twl-lock-helper

all: $(BIN)

build:
	mkdir -p build

build/twl: $(OBJ) | build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	strip --strip-all --remove-section=.comment --remove-section=.note* $@ 2>/dev/null || true

build/%.o: %.c $(HDR) | build
	$(CC) $(CFLAGS) -c $< -o $@

# wall.c embeds stb_image.h; it trips most of our warnings. Compile with -w.
build/wall.o: wall.c $(HDR) | build
	$(CC) $(CFLAGS) -w -c $< -o $@

build/twlctl: twlctl.c | build
	$(CC) $(CFLAGS) twlctl.c -o $@ -Wl,--gc-sections -Wl,-s
	strip --strip-all --remove-section=.comment --remove-section=.note* $@ 2>/dev/null || true

# Separate PAM helper binary — main twl stays libpam-free; only this tiny
# binary links against libpam. One fork+exec per unlock attempt.
build/twl-lock-helper: tools/lock-helper.c | build
	$(CC) $(CFLAGS) tools/lock-helper.c -o $@ -Wl,--gc-sections -Wl,-s -lpam
	strip --strip-all --remove-section=.comment --remove-section=.note* $@ 2>/dev/null || true

# Regenerate baked font header. Requires freetype.
bake.h: tools/bake $(FONT)
	./tools/bake $(FONT) bake.h

tools/bake: tools/bake.c
	$(CC) -O2 -Wall tools/bake.c -o $@ \
	    `pkg-config --cflags --libs freetype2`

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/twl              $(DESTDIR)$(PREFIX)/bin/twl
	install -m 755 build/twlctl           $(DESTDIR)$(PREFIX)/bin/twlctl
	install -m 755 build/twl-lock-helper  $(DESTDIR)$(PREFIX)/bin/twl-lock-helper

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/twl \
	      $(DESTDIR)$(PREFIX)/bin/twlctl \
	      $(DESTDIR)$(PREFIX)/bin/twl-lock-helper

clean:
	rm -rf build tools/bake

# 'make distclean' also removes the baked font header.
distclean: clean
	rm -f bake.h

.PHONY: all install uninstall clean distclean
