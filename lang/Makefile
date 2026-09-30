# BobShit — runtime C de référence
#
#   make                build ./bobshit
#   make run            build then run examples/hello.shit
#   make test           build then run the whole examples/ folder
#   make install        install to $(DESTDIR)$(PREFIX), default /usr/local
#   make uninstall      remove what install put there
#   make examples       list every example script
#   make clean
#
# Distribution packages (deb, rpm, Arch) reuse `make install` with their own
# DESTDIR and PREFIX, so there is a single install layout for every platform.

CC       ?= cc
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra
CPPFLAGS  = -Iinclude
LDFLAGS  ?=
LDLIBS    = -lm

PREFIX  ?= /usr/local
DESTDIR ?=

BINDIR  = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
DOCDIR  = $(DATADIR)/doc/bobshit
MANDIR  = $(DATADIR)/man

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)

BIN := bobshit
EXAMPLES := $(wildcard examples/*.shit)

.PHONY: all clean run test install uninstall examples strict dist

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

run: $(BIN)
	./$(BIN) examples/hello.shit

test: $(BIN)
	@fail=0; \
	for f in $(EXAMPLES); do \
		printf '=== %s\n' "$$f"; \
		./$(BIN) "$$f" || fail=1; \
	done; \
	exit $$fail

examples:
	@for f in $(EXAMPLES); do echo "$$f"; done

strict: $(BIN)
	@for f in $(EXAMPLES); do \
		printf '=== %s (strict)\n' "$$f"; \
		./$(BIN) -s "$$f" >/dev/null || true; \
	done

# ---------------------------------------------------------------------
# install / uninstall
# ---------------------------------------------------------------------
install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -d $(DESTDIR)$(DOCDIR)/examples
	install -m 0644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -m 0644 docs/LANGUAGE.md $(DESTDIR)$(DOCDIR)/LANGUAGE.md
	install -m 0644 $(EXAMPLES) $(DESTDIR)$(DOCDIR)/examples/
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 0644 docs/bobshit.1 $(DESTDIR)$(MANDIR)/man1/bobshit.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(MANDIR)/man1/bobshit.1
	rm -rf $(DESTDIR)$(DOCDIR)

clean:
	rm -f $(OBJ) $(DEP) $(BIN)

-include $(DEP)
