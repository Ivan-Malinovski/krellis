#!/usr/bin/env bash
# Run the Krellis preview on real hardware in its own KWin, on a text VT.
# Log in on e.g. tty3 (Ctrl+Alt+F3), then: ~/dev/krellis/tools/vt-preview.sh
# Ctrl+Alt+F2 returns to the desktop. Closing all three terminals ends it. The live session is untouched.
set -euo pipefail
case "$(tty)" in /dev/tty[0-9]*) ;; *) echo 'Run this from a text VT login (Ctrl+Alt+F3), not inside the desktop.' >&2; exit 1;; esac
[ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ] || { echo 'A graphical session is attached to this shell; log in fresh on the VT.' >&2; exit 1; }
export PATH=$HOME/.local/bin:$PATH
export NEST_BACKEND=drm NEST_SOCKET=krellis-test-vt
exec "$(dirname "$0")/preview.sh"
