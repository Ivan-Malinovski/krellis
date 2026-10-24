#!/usr/bin/env bash
# Pressing Shift in the middle of a drag, without moving the pointer, floats the window at once; releasing it
# puts the drop back to a tile.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
SHIFT=16777248
rc=0
want() { call dump | grep -E "^(DRAG|SLOT)" | head -3; }
call simDragStart 1 640 400 | grep -q true || { echo "FAIL could not grab"; rc=1; }
for x in 600 560 520 500; do call simDragMove $x 300; sleep 0.05; done
sleep 0.4
echo "--- no key"; want
before=$(call dump | grep -E "^DRAG" | head -1)
call simKey $SHIFT true; sleep 0.5
echo "--- Shift pressed, pointer still"; want
after=$(call dump | grep -E "^DRAG" | head -1)
[ "$before" != "$after" ] && echo "PASS Shift changes the drag target without motion" || { echo "FAIL no change after Shift"; rc=1; }
call simKey $SHIFT false; sleep 0.5
back=$(call dump | grep -E "^DRAG" | head -1)
[ "$back" = "$before" ] && echo "PASS releasing Shift restores it" || { echo "FAIL not restored: $back"; rc=1; }
call simDragEnd false; sleep 1
pkill -P "$$" -x alacritty
exit $rc
