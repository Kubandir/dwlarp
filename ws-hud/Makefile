PREFIX ?= $(HOME)/.local
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER   = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

PKGS = wayland-client wayland-cursor fcft pixman-1
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs   $(PKGS)) -lrt

OBJS = ws-hud.o xdg-shell-protocol.o wlr-layer-shell-unstable-v1-protocol.o

all: ws-hud

ws-hud: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

ws-hud.o: ws-hud.c xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h ../config.h
	$(CC) $(CFLAGS) -I.. -I. -c -o $@ ws-hud.c

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header ../dwlb/protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code  ../dwlb/protocols/wlr-layer-shell-unstable-v1.xml $@

install: ws-hud
	install -Dm755 ws-hud $(PREFIX)/bin/ws-hud

clean:
	rm -f ws-hud *.o *-protocol.[ch]

.PHONY: all install clean
