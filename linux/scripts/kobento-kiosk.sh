#!/bin/bash
# Launch the Kobento RC player as a kiosk on a Raspberry Pi's PIXEL/labwc desktop — the
# SSH/remote-proof way. Usage:
#
#   kobento-kiosk.sh [DOCS_DIR] [extra rcplayer flags...]
#
# Defaults to ~/RemoteCompose-Examples and the --pi preset (fullscreen + 1/2 render + nearest).
# Safe to run over SSH or from an autostart entry: it attaches to the running Wayland session and
# detaches stdin, so the player does not quit when the launching shell goes away.
set -euo pipefail

# Attach to the logged-in desktop session (seat 0, user 1000 — the usual Pi default).
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export DISPLAY="${DISPLAY:-:0}"
# Native Wayland on labwc (wlroots); fall back to X11 via XWayland if the Wayland path is absent.
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland,x11}"

BIN="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/rcplayer"
[ -x "$BIN" ] || BIN="$HOME/kobento/build/rcplayer"   # common install location on the Pi

DOCS="${1:-$HOME/RemoteCompose-Examples}"
shift 2>/dev/null || true

# stdin from /dev/null: the console is unavailable (fine for kiosk), and EOF no longer triggers a
# quit, so the window keys / touch / --auto drive the player until it is closed.
# A crash restarts the player. Q/Esc (exit 0) and `pkill rcplayer` (SIGTERM, 143) stop it.
[ -t 1 ] || exec >"$HOME/kiosk.log" 2>&1   # from autostart or nohup: keep the banner and errors
while true; do
    rc=0
    stdbuf -oL -eL "$BIN" "$DOCS" --pi "$@" </dev/null || rc=$?
    case "$rc" in 0|143) exit 0 ;; esac
    echo "rcplayer exited with $rc; restarting" >&2
    sleep 3
done
