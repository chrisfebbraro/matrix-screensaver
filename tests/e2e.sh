#!/bin/bash
# Manual end-to-end check on a live Omarchy desktop: launch through the login shell the way
# the idle service does, confirm the fullscreen window, dismiss with a synthetic keypress.
# Needs wtype. Run from anywhere; do not put the window class string on your command line,
# because the launcher's "already running" check is a pgrep -f.
CLS="org.omarchy.screensaver"
env -i HOME="$HOME" USER="$USER" OMARCHY_PATH=/usr/share/omarchy PATH="/usr/share/omarchy/bin:/usr/local/bin:/usr/bin" \
  XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" HYPRLAND_INSTANCE_SIGNATURE="$HYPRLAND_INSTANCE_SIGNATURE" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
  bash -lc 'omarchy-launch-screensaver force'
sleep 4
hyprctl clients -j | jq -r --arg c "$CLS" '.[] | select(.class==$c) | "window class=\(.class) size=\(.size) fullscreen=\(.fullscreen)"'
echo "renderer: $(pgrep -a -x matrix-rain-tty | cut -d' ' -f2-)"
echo "-> keypress"; wtype x; sleep 1.5
echo "after keypress: renderer=$(pgrep -c -x matrix-rain-tty) windows=$(hyprctl clients -j | jq -r --arg c "$CLS" '[.[] | select(.class==$c)] | length')"
