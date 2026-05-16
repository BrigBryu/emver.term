CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic

HAVE_PKG_CONFIG := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo yes || echo no)

ifeq ($(HAVE_PKG_CONFIG),yes)
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags ncursesw 2>/dev/null || $(PKG_CONFIG) --cflags ncurses 2>/dev/null)
LDFLAGS += $(shell $(PKG_CONFIG) --libs ncursesw 2>/dev/null || $(PKG_CONFIG) --libs ncurses 2>/dev/null)
else
LDFLAGS += -lncurses
endif

SRC := src/main.c src/cli.c src/engine.c src/live.c
OBJ := $(SRC:.c=.o)
BIN := ember-term

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -lm -o $@

%.o: %.c src/ember.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

test: $(BIN)
	./tests/smoke.sh

clean:
	rm -f $(OBJ) $(BIN)
