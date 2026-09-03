# Matrix Digital Rain screensaver

A recreation of the Matrix "digital rain" as a screensaver for this Omarchy / Hyprland
desktop, closely following the "classic" mode of Rezmason's
[matrix](https://github.com/Rezmason/matrix) (MIT). Same ingredients:

- the real film glyphs (Rezmason's `matrixcode` MSDF atlas, embedded in the page),
- stationary glyphs in a fixed grid, lit by wobbling sawtooth "raindrops" per column,
- a bright, separately coloured cursor at the tip of each raindrop,
- a 5-level bloom pyramid, then tone-mapping through the green palette with dither.

## Files

| File | Purpose |
| --- | --- |
| `matrix.html` | Self-contained WebGL page. Open it in any browser; double-click for fullscreen. |
| `omarchy-launch-screensaver` | Drop-in replacement for Omarchy's launcher. Opens `matrix.html` in a Chromium kiosk window with Omarchy's screensaver window class, one per monitor, and tears it down on input. |
| `~/.local/bin/omarchy-launch-screensaver` | Symlink to the script above. |

## How it is wired in

Omarchy's shell runs `omarchy-launch-screensaver` after `idle.screensaver` seconds
(`~/.config/omarchy/shell.json`, currently 600 s) and locks after `idle.lock` (900 s).
The Omarchy menu's "Screensaver" entry runs the same command. `~/.bashrc` now puts
`~/.local/bin` ahead of `$OMARCHY_PATH/bin`, so both pick up this launcher instead of
the stock terminal (ttfx) one. If Chromium is missing, the launcher falls back to the
stock screensaver.

The kiosk window uses the class `org.omarchy.screensaver`, so Omarchy's existing window
rules (fullscreen, float, slide animation), its idle service, and `omarchy-system-lock`
treat it exactly like the built-in screensaver.

Dismissal: the page changes its title on any key press, click, scroll, or mouse
movement, and a small watchdog reading Hyprland's event socket kills the browser when it
sees that title (or when focus moves to another window, or all screensaver windows close).

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

Query parameters on `matrix.html` (the launcher's `PAGE` variable) override the defaults:
`numColumns` (160; glyph size is screen width divided by this), `fallSpeed` (0.3), `cycleSpeed` (0.03), `raindropLength` (0.75),
`bloomStrength` (0.7), `bloomSize` (0.4), `animationSpeed` (1), `resolution` (1, render
scale), `fps` (60). For example `matrix.html?numColumns=60&fallSpeed=0.5`.

Set `MATRIX_BROWSER` to use another Chromium-based browser for the kiosk window.
