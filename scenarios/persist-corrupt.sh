#!/usr/bin/env bash
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
trap 'pkill -P "$$" -x alacritty || true' EXIT
cp "$XDG_STATE_HOME/kwin-trellis/session.json" "$NEST_OUT/original.json"
call sessionStatus | grep "error 'Unsupported session schema'"
alacritty --title corrupt-session-test -e sleep 600 >/dev/null 2>&1 &
sleep 2
call saveSession | grep false
cmp "$XDG_STATE_HOME/kwin-trellis/session.json" "$NEST_OUT/original.json"
test "$(call dump | grep -c ' screen ')" -eq 1
echo 'PASS corrupt session is rejected and preserved while the canvas remains usable'
