# Matrix Digital Rain screensaver

A recreation of the Matrix "digital rain" as a screensaver for this Omarchy / Hyprland
desktop, closely following the "classic" mode of Rezmason's
[matrix](https://github.com/Rezmason/matrix) (MIT). Same ingredients:

- the real film glyphs (Rezmason's `matrixcode` MSDF atlas, embedded),
- stationary glyphs in a fixed grid, lit by wobbling sawtooth "raindrops" per column,
- a bright, separately coloured cursor at the tip of each raindrop,
- a 5-level bloom pyramid, then tone-mapping through the green palette with dither.

It comes in two renderers that share the same shaders:

| | `native/matrix-rain` (SDL3 + OpenGL ES 3) | `matrix.html` (WebGL in Chromium) |
| --- | --- | --- |
| Memory while running | ~160 MB, one process | ~1.3 GB across ~10 processes |
| Start-up | instant | a second or two |
| Dependencies | `sdl3`, Mesa | Chromium |
| Use | the screensaver | preview in any browser, fallback if the binary is not built |

## Files

| File | Purpose |
| --- | --- |
| `native/matrix-rain.c` | The native program. Shaders and glyph atlas are embedded at compile time (C23 `#embed`). |
| `native/shaders/*.glsl` | The five passes (rain, high-pass, blur, combine, palette) plus the fullscreen-triangle vertex shader. |
| `native/matrixcode_msdf.bmp` | Rezmason's glyph atlas, as a BMP so SDL can load it without extra libraries. |
| `matrix.html` | The same effect as a self-contained web page, atlas and shaders inlined. Double-click for fullscreen. |
| `omarchy-launch-screensaver` | Drop-in replacement for Omarchy's launcher: one fullscreen window per monitor with Omarchy's screensaver window class, torn down on input. |
| `~/.local/bin/omarchy-launch-screensaver` | Symlink to the script above. |

## Building the native renderer

```bash
make -C native            # needs gcc >= 15 (or clang >= 19), sdl3, mesa
native/matrix-rain --windowed          # try it in a window; Escape quits
native/matrix-rain --help              # all options
```

The launcher uses the binary automatically once it exists.

## How it is wired in

Omarchy's shell runs `omarchy-launch-screensaver` after `idle.screensaver` seconds
(`~/.config/omarchy/shell.json`, currently 600 s) and locks after `idle.lock` (900 s).
The Omarchy menu's "Screensaver" entry runs the same command. `~/.bashrc` puts
`~/.local/bin` ahead of `$OMARCHY_PATH/bin`, so both pick up this launcher instead of
the stock terminal (ttfx) one.

The launcher prefers `native/matrix-rain`, falls back to a Chromium kiosk window showing
`matrix.html`, and finally to Omarchy's stock screensaver. Either window uses the class
`org.omarchy.screensaver`, so Omarchy's window rules (fullscreen, float, slide
animation), its idle service, and `omarchy-system-lock` treat it exactly like the
built-in screensaver.

Dismissal: the native program exits on any key, click, scroll, or mouse movement (after a
1.5 s grace period). The web page instead changes its title, and a small watchdog reading
Hyprland's event socket kills the browser when it sees that. The watchdog also stops the
screensaver if focus moves to another window or all screensaver windows close.

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

Native: pass options through `MATRIX_ARGS`, for example
`MATRIX_ARGS="--columns 120 --fall-speed 0.5" omarchy-launch-screensaver force`, or set
it in the environment the Omarchy shell sees. Options: `--columns` (160; glyph size is
screen width divided by this), `--fall-speed` (0.3), `--cycle-speed` (0.03),
`--raindrop-length` (0.75), `--bloom-strength` (0.7), `--bloom-size` (0.4),
`--animation-speed` (1), `--resolution` (1, render scale), `--fps` (60; 0 follows the
display refresh rate).

Web page: the same settings as query parameters on `matrix.html` (the launcher's `PAGE`
variable), e.g. `matrix.html?numColumns=60&fallSpeed=0.5`. Set `MATRIX_BROWSER` to use
another Chromium-based browser for the kiosk window.
