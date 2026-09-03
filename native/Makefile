# Native Matrix digital rain.
#   matrix-rain      SDL3 + OpenGL ES 3 (needs gcc >= 15 or clang >= 19 for C23 #embed, sdl3, Mesa)
#   matrix-rain-tty  plain terminal renderer, no dependencies beyond libc/libm

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra

all: matrix-rain matrix-rain-tty

matrix-rain: matrix-rain.c shaders/quad.vert.glsl shaders/rain.frag.glsl shaders/highpass.frag.glsl \
             shaders/blur.frag.glsl shaders/combine.frag.glsl shaders/palette.frag.glsl matrixcode_msdf.bmp
	$(CC) $(CFLAGS) -std=c23 $(shell pkg-config --cflags sdl3) -o $@ $< $(shell pkg-config --libs sdl3) -lGLESv2 -lm

matrix-rain-tty: matrix-rain-tty.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $< -lm

clean:
	rm -f matrix-rain matrix-rain-tty

.PHONY: all clean
