#!/bin/bash
# Install or remove the Matrix digital rain screensaver on an Omarchy desktop.
#
#   ./install.sh             build, link the launcher into ~/.local/bin, make it win over
#                            Omarchy's launcher, and verify
#   ./install.sh uninstall   remove the link and the PATH line
#
# Safe to run again; every step is skipped when it is already done.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$HOME/.local/bin"
LINK="$BIN_DIR/omarchy-launch-screensaver"
BASHRC="$HOME/.bashrc"
MARKER="# matrix-screensaver: user overrides in ~/.local/bin take precedence over Omarchy's bin"
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'

say() { printf '%s\n' "$*"; }
fail() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

# Does a login shell, started the way Omarchy's idle service starts one, find our launcher?
resolves_to_ours() {
  local found
  found=$(env -i HOME="$HOME" USER="${USER:-$(id -un)}" OMARCHY_PATH="${OMARCHY_PATH:-/usr/share/omarchy}" \
    PATH="${OMARCHY_PATH:-/usr/share/omarchy}/bin:/usr/local/bin:/usr/bin" \
    bash -lc 'command -v omarchy-launch-screensaver' 2>/dev/null || true)
  [[ $found == "$LINK" ]]
}

remove_path_line() {
  [[ -f $BASHRC ]] || return 0
  grep -qF "$MARKER" "$BASHRC" || return 0
  cp "$BASHRC" "$BASHRC.bak.matrix-screensaver"
  # Drop the marker comment and the export line that follows it.
  awk -v marker="$MARKER" -v line="$PATH_LINE" '
    $0 == marker { skip = 1; next }
    skip && $0 == line { skip = 0; next }
    { skip = 0; print }
  ' "$BASHRC" > "$BASHRC.tmp" && mv "$BASHRC.tmp" "$BASHRC"
  say "removed the PATH line from $BASHRC (backup in $BASHRC.bak.matrix-screensaver)"
}

if [[ ${1:-} == uninstall ]]; then
  if [[ -L $LINK ]]; then rm "$LINK"; say "removed $LINK"; else say "no launcher link to remove"; fi
  remove_path_line
  say "Omarchy's stock screensaver is back in charge."
  exit 0
fi
[[ -z ${1:-} ]] || fail "unknown argument '$1' (use: install.sh [uninstall])"

[[ -d ${OMARCHY_PATH:-/usr/share/omarchy} ]] || fail "this installs into an Omarchy desktop, and /usr/share/omarchy is missing"
command -v gcc >/dev/null || command -v cc >/dev/null || fail "no C compiler found (pacman -S gcc)"

# 1. Build
say "building matrix-rain-tty"
make -s -C "$HERE"

# 2. Link the launcher where the PATH will find it
mkdir -p "$BIN_DIR"
if [[ $(readlink -f "$LINK" 2>/dev/null) == "$HERE/omarchy-launch-screensaver" ]]; then
  say "launcher already linked at $LINK"
elif [[ -e $LINK || -L $LINK ]]; then
  fail "$LINK exists and is not ours; move it aside and run again"
else
  ln -s "$HERE/omarchy-launch-screensaver" "$LINK"
  say "linked $LINK"
fi

# 3. Make ~/.local/bin win over Omarchy's bin, for login shells too. Omarchy's ~/.bashrc
#    sets PATH from its env-bootstrap line and then returns early for non-interactive
#    shells, so the line has to sit right after the bootstrap and above that return.
if resolves_to_ours; then
  say "PATH already prefers $BIN_DIR"
else
  [[ -f $BASHRC ]] || fail "$BASHRC not found; add this line to your shell startup yourself: $PATH_LINE"
  cp "$BASHRC" "$BASHRC.bak.matrix-screensaver"
  if grep -q 'default/bash/env-bootstrap' "$BASHRC"; then
    awk -v marker="$MARKER" -v line="$PATH_LINE" '
      { print }
      !done && /default\/bash\/env-bootstrap/ { print ""; print marker; print line; done = 1 }
    ' "$BASHRC" > "$BASHRC.tmp" && mv "$BASHRC.tmp" "$BASHRC"
  else
    { printf '\n%s\n%s\n' "$MARKER" "$PATH_LINE"; cat "$BASHRC"; } > "$BASHRC.tmp" && mv "$BASHRC.tmp" "$BASHRC"
  fi
  say "added the PATH line to $BASHRC (backup in $BASHRC.bak.matrix-screensaver)"
fi

# 4. A settings file to edit, unless one exists already
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/matrix-screensaver"
if [[ -f $CONFIG_DIR/config ]]; then
  say "keeping your settings in $CONFIG_DIR/config"
else
  mkdir -p "$CONFIG_DIR"
  cp "$HERE/config.example" "$CONFIG_DIR/config"
  say "wrote default settings to $CONFIG_DIR/config (edit it to tune the rain)"
fi

# 5. Verify the way it will really be used
if resolves_to_ours; then
  say "verified: a login shell resolves omarchy-launch-screensaver to $LINK"
else
  fail "a login shell still does not find $LINK first; check the PATH line in $BASHRC sits above the interactive-shell return"
fi

say
say "Installed. Omarchy starts it after idle.screensaver seconds (~/.config/omarchy/shell.json)"
say "and from the menu's Screensaver entry. Try it now:  omarchy-launch-screensaver force"
