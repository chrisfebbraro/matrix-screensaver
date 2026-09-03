# Matrix digital rain screensaver for Omarchy

The Matrix "digital rain" as it appears on the operators' screens in the films, drawn in
your terminal, wired in as the screensaver on an [Omarchy](https://omarchy.org) desktop.

- Half-width katakana and digits in plain text, a bright head leading each drop, and a
  stream that fades to black behind it.
- Every column falls at its own speed, drawn from a normal distribution with a band you
  set and fast and slow outliers, and spawns drops at intervals that grow with its speed.
- Colours follow your Omarchy theme: the stream takes the theme's accent, the head its
  bright foreground.
- Costs about 2 MB and a few percent of one core; the terminal does the drawing.

## Install

Omarchy 4 with foot, Alacritty, Ghostty or kitty as the default terminal.

```bash
git clone https://github.com/chrisfebbraro/matrix-screensaver ~/Projects/matrix-screensaver
~/Projects/matrix-screensaver/install.sh
```

The script builds the renderer, links the launcher into `~/.local/bin`, adds one line to
`~/.bashrc` so that directory is searched before Omarchy's own (placed right after
Omarchy's PATH setup, where the Omarchy shell's login shells see it), and verifies that a
login shell now resolves `omarchy-launch-screensaver` to this one. Run it again after a
`git pull` to rebuild; every step is skipped when already done.

From then on Omarchy's idle service, which runs `omarchy-launch-screensaver` after
`idle.screensaver` seconds (`~/.config/omarchy/shell.json`), and the Omarchy menu's
"Screensaver" entry both start this screensaver: your default terminal with Omarchy's
screensaver config and your system monospace font, one fullscreen window per monitor.
If the binary is missing it falls back to Omarchy's stock screensaver.

Try it now: `omarchy-launch-screensaver force`. Any key dismisses it.

```bash
~/Projects/matrix-screensaver/install.sh uninstall   # removes the link and the PATH line
```

## Files

| File | Purpose |
| --- | --- |
| `matrix-rain-tty.c` | The renderer. Plain C11, needs only libm; raw ANSI escapes, 24-bit colour, redraws only changed cells. |
| `omarchy-launch-screensaver` | Drop-in replacement for Omarchy's launcher. |
| `install.sh` | Builds, links, fixes the PATH order and verifies; `install.sh uninstall` reverses it. |
| `Makefile` | `make` builds `matrix-rain-tty`. |
| `tests/e2e.sh` | Manual live check: launch as the idle service would, confirm the window, dismiss with a synthetic keypress. |

## Tuning

Everything is a command-line option on `matrix-rain-tty` (see `--help`); the launcher
passes `MATRIX_TTY_ARGS` through, so for example

```bash
MATRIX_TTY_ARGS="--trail 12 --speed-k 0.8" omarchy-launch-screensaver force
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--fall-speed F` | 0.105 | Base speed: a column at relative speed 1 falls 100·F rows per second. |
| `--speed-min F`, `--speed-max F` | 0.5, 1.5 | Column speeds are normally distributed, relative to the fall speed, with about 95% of columns inside this band. The rest are faster or slower outliers, clamped so nothing stops. |
| `--speed-k F` | 0.9 | Scales the whole band together. |
| `--period F` | 0.8 | Base spacing between drops in a column: 100·F rows at the mean speed, jittered per drop. |
| `--period-k F` | 0.8 | Faster columns space their drops out more: spacing = base · (speed / mean)^k. 0 gives every column the same spacing. |
| `--trail N` | 20 | Rows lit behind each head. Drops never come closer than this, so they never touch. |
| `--gap N` | 0 | Blank columns between streams. |
| `--cycle-speed F` | 0.005 | How often glyphs change in place, per 1/60 s. |
| `--head C` | white | Head colour: `white`, `green`, or hex `RRGGBB`. |
| `--stream RRGGBB` | green | Stream colour; it fades to black towards the tail. |
| `--flat` | off | One colour for the whole stream instead of the fade. |
| `--fps N` | 30 | Frame rate. |

Launcher environment: `MATRIX_TTY_FONT_SIZE` (9), `MATRIX_TTY_LINE_HEIGHT` (14px; the row
height for foot and kitty, an adjustment for Alacritty and Ghostty, applied only when
set), `MATRIX_TTY_THEME` (1; 0 keeps the film green instead of the theme colours).

Idle timing lives in Omarchy: `idle.screensaver` and `idle.lock` in
`~/.config/omarchy/shell.json`, in seconds.
