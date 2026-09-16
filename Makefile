CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS += -lm

.PHONY: all run test clean
all: gkbasic

gkbasic: src/basic.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

run: gkbasic
	./gkbasic

test: gkbasic
	python3 tests/test_basic.py

clean:
	rm -f gkbasic
