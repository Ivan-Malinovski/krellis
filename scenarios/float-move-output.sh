#!/usr/bin/env bash
# Run with NEST_OUTPUTS=2. A float (keep-above) that reads as undecorated, sent to the other output, must stay tracked:
# the hand-over used to re-run manageable(), the "undecorated, always on top" helper test refused it after it had been
# detached, and it was left untracked and impossible to dock again (seen after Shift-dragging Brave across monitors).
set -uo pipefail
[[ "$NEST_SOCKET" == krellis-test-* ]]
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
rc=0
alacritty --title "win-A" -o 'window.decorations="None"' -e sleep 600 >/dev/null 2>&1 & sleep 1.5
alacritty --title "win-B" -e sleep 600 >/dev/null 2>&1 & sleep 1.5
sleep 1
count() { call dumpOutput "$1" | grep -c ' screen ' || true; }
call selectOutput 0
idx=$(call dumpOutput 0 | awk '/ screen / { n++; if (index($0, "win-A")) { print n-1; exit } }')
call toggleFloat "$idx"; sleep 1
call dumpOutput 0 | grep -E ' screen |ORDER'
call moveToOutput "$idx" 1; sleep 1.5
echo "after: out0=$(count 0) out1=$(count 1)"
call dumpOutput 1 | grep -E ' screen |ORDER'
[ "$(count 1)" = 1 ] && echo "PASS the float is tracked on the other output" || { echo "FAIL the float was lost"; rc=1; }
[ "$(count 0)" = 1 ] && echo "PASS the other window stays" || { echo "FAIL source count $(count 0)"; rc=1; }
pkill -P "$$" -x alacritty
exit $rc
