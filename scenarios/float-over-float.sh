# Dropping a float onto another float (which sits over a tile) must not snap back: it stays a float where it is dropped.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
rc=0
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
floats() { call dump | grep -c ' FLOAT$'; }
pos() { call dump | grep "'win-$1'" | awk '{ print $4 }'; }
ok() { echo "ok $1"; }
bad() { echo "FAIL $1"; rc=1; }
drag() {  # index fromX fromY toX toY
    call simDragStart "$1" "$2" "$3" | grep -q true || { bad "could not grab $1"; return; }
    for i in 1 2 3 4; do call simDragMove "$4" "$5"; sleep 0.05; done
    sleep 0.4; call dump | grep -E "^DRAG"
    call simDragEnd true; sleep 1.3
}
call simForceFloat 1
drag 1 640 400 300 300
drag 2 640 400 380 340
call simForceFloat 0
echo "floats: $(floats) order: $(order)"
call dump | grep FLOAT | cut -c1-120
before=$(pos B)
# C is the top float, B below it; the pointer ends over both, and a tile lies beneath.
call simForceFloat 1
drag 1 150 300 400 360
call simForceFloat 0
[ "$(floats)" = 2 ] && ok "still two floats after Shift drop onto a float" || bad "shift drop: $(floats) floats, order '$(order)'"
echo "B before $before after $(pos B)"
# Without Shift, from a float onto another float.
drag 1 400 360 420 380
[ "$(floats)" = 2 ] && [ "$(pos B)" != "$before" ] && ok "plain drop onto a float keeps it where dropped ($(pos B))" || bad "plain drop: $(floats) floats, B at $(pos B), order '$(order)'"
pkill -P "$$" -x alacritty
exit $rc
