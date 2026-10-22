# Holding Shift (simulated) while dragging floats a tiled window where it is dropped, and keeps a float from docking.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
echo "start: $(order)"
rc=0
check() { [ "$(order)" = "$2" ] && echo "ok $1: $(order)" || { echo "FAIL $1: got '$(order)' want '$2'"; rc=1; }; }
check start "p1 p2 p3"
# A plain drag of B to the middle of the screen would reflow the tiles; with Shift it floats instead.
call simForceFloat 1
call simDragStart 1 640 400
for x in 600 560 520 500; do call simDragMove $x 300; sleep 0.05; done
sleep 0.4; call dump | grep -E "DRAG|SLOT"
call simDragEnd true; sleep 1.3
check float-drop "p1 p3"
# With Shift still held the float cannot dock, even dragged into the right edge band.
call simDragStart 1 500 300 | grep -q true || { echo "FAIL could not grab the float"; rc=1; }
for x in 800 1100 1250 1277; do call simDragMove $x 400; sleep 0.05; done
sleep 0.4
call simDragEnd true; sleep 1.3
check stays-float "p1 p3"
# Released, the same drag docks it again.
call simForceFloat 0
call simDragStart 1 1100 400 | grep -q true || { echo "FAIL could not grab the float again"; rc=1; }
for x in 1200 1250 1277; do call simDragMove $x 400; sleep 0.05; done
sleep 0.4
call simDragEnd true; sleep 1.3
[ "$(order | wc -w)" = 3 ] && echo "ok docks without Shift: $(order)" || { echo "FAIL dock: $(order)"; rc=1; }
call simForceFloat -1
pkill -P "$$" -x alacritty
exit $rc
