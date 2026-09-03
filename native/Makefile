# Native Matrix digital rain: SDL3 + OpenGL ES 3.
# Needs gcc >= 15 or clang >= 19 (C23 #embed), sdl3, and Mesa's libGLESv2.

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra
CPPFLAGS += -std=c23 $(shell pkg-config --cflags sdl3)
LDLIBS   += $(shell pkg-config --libs sdl3) -lGLESv2 -lm

matrix-rain: matrix-rain.c shaders/quad.vert.glsl shaders/rain.frag.glsl shaders/highpass.frag.glsl \
             shaders/blur.frag.glsl shaders/combine.frag.glsl shaders/palette.frag.glsl matrixcode_msdf.bmp
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f matrix-rain

.PHONY: clean
