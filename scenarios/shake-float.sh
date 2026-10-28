# Shaking a window by its title bar (a quick back-and-forth) floats it where it is dropped, like Shift-drag.
# A plain drag, a slow wiggle and the setting turned off leave it tiled.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
rc=0
check() { [ "$(order)" = "$2" ] && echo "ok $1: $(order)" || { echo "FAIL $1: got '$(order)' want '$2'"; rc=1; }; }
check start "p1 p2 p3"
shake() { # shake <pause between moves>
    for x in 520 380 520 380 520 380 520 380 500; do call simDragMove $x 300; sleep "$1"; done
}
# Moves into the middle of the screen with no shake: the drop tiles, so nothing floats (ORDER keeps all three).
call simDragStart 1 640 400
for x in 600 560 520 500; do call simDragMove $x 300; sleep 0.05; done
call simDragEnd true; sleep 1.3
[ "$(order | wc -w)" = 3 ] && echo "ok plain drag stays tiled: $(order)" || { echo "FAIL plain drag: $(order)"; rc=1; }
# Slow wiggle: same swings, but 400 ms apart.
call simDragStart 1 640 400 | grep -q true || { echo "FAIL could not grab"; rc=1; }
shake 0.4
call simDragEnd true; sleep 1.3
[ "$(order | wc -w)" = 3 ] && echo "ok slow wiggle stays tiled: $(order)" || { echo "FAIL slow wiggle: $(order)"; rc=1; }
# A quick shake floats it: ORDER lists only the tiles that remain.
call simDragStart 1 640 400 | grep -q true || { echo "FAIL could not grab"; rc=1; }
shake 0.04
sleep 0.3; call dump | grep -E "DRAG|SLOT"
call simDragEnd true; sleep 1.3
[ "$(order | wc -w)" = 2 ] && echo "ok shake floats it: $(order)" || { echo "FAIL shake: $(order)"; rc=1; }
# The latch ends with the drag: the next plain drag of a tile docks as usual.
call dump | grep -c FLOAT | xargs echo floats:
pkill -P "$$" -x alacritty
exit $rc
