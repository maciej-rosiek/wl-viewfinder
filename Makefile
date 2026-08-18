PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Ibuild
LDLIBS += $(shell pkg-config --libs wayland-client)
CFLAGS += $(shell pkg-config --cflags wayland-client)

WAYLAND_SCANNER ?= $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)

PROTOCOLS := wlr-layer-shell-unstable-v1 xdg-shell
PROTO_HEADERS := $(PROTOCOLS:%=build/%-client-protocol.h)
PROTO_SOURCES := $(PROTOCOLS:%=build/%-protocol.c)

all: build/wl-viewfinder-frame

build:
	mkdir -p build

build/%-client-protocol.h: protocol/%.xml | build
	$(WAYLAND_SCANNER) client-header $< $@

build/%-protocol.c: protocol/%.xml | build
	$(WAYLAND_SCANNER) private-code $< $@

build/wl-viewfinder-frame: frame/main.c $(PROTO_SOURCES) $(PROTO_HEADERS) | build
	$(CC) $(CFLAGS) -o $@ frame/main.c $(PROTO_SOURCES) $(LDLIBS)

install: all
	install -Dm755 build/wl-viewfinder-frame $(DESTDIR)$(BINDIR)/wl-viewfinder-frame
	install -Dm755 wl-viewfinder $(DESTDIR)$(BINDIR)/wl-viewfinder

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/wl-viewfinder-frame $(DESTDIR)$(BINDIR)/wl-viewfinder

clean:
	rm -rf build

.PHONY: all install uninstall clean
