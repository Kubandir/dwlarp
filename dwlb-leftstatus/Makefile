PREFIX  ?= $(HOME)/.local
CFLAGS  ?= -O2 -Wall -Wextra

dwlb-leftstatus: dwlb-leftstatus.c
	$(CC) $(CFLAGS) -o $@ $<

install: dwlb-leftstatus
	install -Dm755 dwlb-leftstatus $(PREFIX)/bin/dwlb-leftstatus

clean:
	rm -f dwlb-leftstatus

.PHONY: install clean
