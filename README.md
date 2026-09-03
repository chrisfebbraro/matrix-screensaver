# Matrix Digital Rain screensaver

The Matrix "digital rain" as a screensaver for this Omarchy / Hyprland desktop, in three
renderers that share the same raindrop maths (after Rezmason's
[matrix](https://github.com/Rezmason/matrix), MIT):

| Renderer | Look | Cost while running |
| --- | --- | --- |
| `native/matrix-rain-tty` | **Terminal.** The code as it appears on the operators' screens and in the first film's titles: flat green half-width katakana and digits, a bright leading glyph per drop, no glow. Runs inside your terminal (foot, Alacritty, Ghostty or kitty). | ~2 MB + the terminal (~55 MB), a few % of one core |
| `native/matrix-rain` | **GPU.** The glossy title-sequence look with the real film glyph atlas, bloom and a green gradient (SDL3 + OpenGL ES 3). | ~160 MB, one process |
| `matrix.html` | The GPU look as a self-contained web page; preview in any browser, or a Chromium kiosk fallback. | ~1.3 GB of Chromium |

Common to all three: stationary glyphs in a fixed grid lit by wobbling sawtooth
"raindrops" per column (multiple drops per column, never colliding), a cursor at the tip
of each drop, and per-cell glyph cycling.

## Choosing the renderer

The one-word file `renderer` next to the launcher selects `terminal`, `native` or
`browser` (the environment variable `MATRIX_RENDERER` overrides it). It is set to
`terminal`. If the chosen renderer is not built or installed, the launcher falls back to
the best available, and finally to Omarchy's stock screensaver.

## Building

```bash
make -C native            # both binaries; needs gcc >= 15 (or clang >= 19), sdl3, mesa
native/matrix-rain-tty                 # try the terminal one right here; q quits
native/matrix-rain --windowed          # try the GPU one in a window; Escape quits
native/matrix-rain-tty --help          # options (also --help on matrix-rain)
```

`matrix-rain-tty` only needs a C compiler. Its shaders, atlas and the SDL3 build are
only needed for `matrix-rain`.

## Files

| File | Purpose |
| --- | --- |
| `native/matrix-rain-tty.c` | Terminal renderer: raw ANSI escapes and 24-bit colour, redraws only changed cells. |
| `native/matrix-rain.c` | GPU renderer. Shaders and glyph atlas are embedded at compile time (C23 `#embed`). |
| `native/shaders/*.glsl` | The five GPU passes (rain, high-pass, blur, combine, palette) plus the fullscreen-triangle vertex shader. |
| `native/matrixcode_msdf.bmp` | Rezmason's glyph atlas, as a BMP so SDL can load it without extra libraries. |
| `matrix.html` | The GPU effect as a web page, atlas and shaders inlined. Double-click for fullscreen. |
| `omarchy-launch-screensaver` | Drop-in replacement for Omarchy's launcher: one fullscreen window per monitor with Omarchy's screensaver window class, torn down on input. |
| `renderer` | Which renderer the launcher uses. |
| `~/.local/bin/omarchy-launch-screensaver` | Symlink to the launcher. |

## How it is wired in

Omarchy's shell runs `omarchy-launch-screensaver` after `idle.screensaver` seconds
(`~/.config/omarchy/shell.json`, currently 600 s) and locks after `idle.lock` (900 s).
The Omarchy menu's "Screensaver" entry runs the same command. `~/.bashrc` puts
`~/.local/bin` ahead of `$OMARCHY_PATH/bin`, so both pick up this launcher instead of
the stock one.

Every renderer's window uses the class `org.omarchy.screensaver`, so Omarchy's window
rules (fullscreen, float, slide animation), its idle service, and `omarchy-system-lock`
treat it exactly like the built-in screensaver. The terminal renderer is started the
same way Omarchy starts its own: your default terminal with Omarchy's screensaver
terminal config (black background, large font, no padding), and the pointer hidden.

Dismissal: the terminal renderer exits on any key; the GPU renderer on any key, click,
scroll, or mouse movement (after a 1.5 s grace period); the web page changes its title
and a watchdog reading Hyprland's event socket kills the browser. The watchdog also stops
the screensaver if focus moves to another window or all screensaver windows close.

## Manual use

```bash
omarchy-launch-screensaver force   # start now (ignores the screensaver-off toggle)
omarchy-launch-screensaver stop    # kill it
```

To go back to Omarchy's stock screensaver, delete the symlink:

```bash
rm ~/.local/bin/omarchy-launch-screensaver
```

## Tweaks

Terminal: `MATRIX_TTY_FONT_SIZE` sets the terminal font size (default 9; Omarchy's own
screensaver uses 18). `MATRIX_TTY_LINE_HEIGHT` spaces the rows out without changing the
glyph size, e.g. `18px` (the row height for foot and kitty; extra pixels per row for
Alacritty and Ghostty). `MATRIX_TTY_ARGS` passes options through, e.g.
`MATRIX_TTY_ARGS="--style classic --gap 1" omarchy-launch-screensaver force`.
Options: `--style operator|classic` (operator: flat, film operator screens; classic:
the title gradient in a few shades), `--head white|green` (colour of the leading glyph; white), `--flat` (one green for the
whole stream instead of fading to black behind the head), `--gap N` (blank columns between streams, 0),
`--fall-speed` (0.105 operator / 0.3 classic), `--cycle-speed` (0.005 / 0.03, per 1/60 s),
`--raindrop-length` (0.8 / 0.35; larger means fewer drops per column), `--trail N` (rows lit
behind each head; 20 operator, 0 classic = tied to the period), `--fps` (30),
`--animation-speed` (1).

GPU: `MATRIX_ARGS` does the same for `matrix-rain`: `--columns` (160; glyph size is
screen width divided by this), `--fall-speed` (0.3), `--cycle-speed` (0.03),
`--raindrop-length` (0.75), `--bloom-strength` (0.7), `--bloom-size` (0.4),
`--animation-speed` (1), `--resolution` (1, render scale), `--fps` (60; 0 follows the
display refresh rate).

Web page: the same settings as query parameters on `matrix.html` (the launcher's `PAGE`
variable), e.g. `matrix.html?numColumns=60&fallSpeed=0.5`. Set `MATRIX_BROWSER` to use
another Chromium-based browser for the kiosk window.
