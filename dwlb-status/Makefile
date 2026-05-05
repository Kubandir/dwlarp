PREFIX  ?= $(HOME)/.local
CFLAGS  ?= -O2 -Wall -Wextra
PKGS     = libpulse
PKGCF   := $(shell pkg-config --cflags $(PKGS))
PKGLIB  := $(shell pkg-config --libs   $(PKGS))

dwlb-status: dwlb-status.c
	$(CC) $(CFLAGS) $(PKGCF) -o $@ $< $(PKGLIB)

install: dwlb-status
	install -Dm755 dwlb-status $(PREFIX)/bin/dwlb-status

clean:
	rm -f dwlb-status

.PHONY: install clean
