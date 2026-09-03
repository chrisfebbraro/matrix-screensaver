# Matrix digital rain for the terminal. Needs only a C compiler and libm.
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

matrix-rain-tty: matrix-rain-tty.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $< -lm

clean:
	rm -f matrix-rain-tty

.PHONY: clean
