# An edge drop with no tiles takes half the screen, or the room left beside the floats that are in the way.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
call setFloatFirst true
for n in A B; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
rc=0
ok() { echo "ok $1"; }
bad() { echo "FAIL $1"; rc=1; }
drag() {  # index fromX fromY toX toY
    call simDragStart "$1" "$2" "$3" | grep -q true || { bad "could not grab $1"; return; }
    for i in 1 2 3 4; do call simDragMove "$4" "$5"; sleep 0.05; done
    sleep 0.4; call dump | grep -E "^(DRAG|SNAP)"
    call simDragEnd true; sleep 1.3
}
drag 1 640 300 420 300
call dump | grep "win-B" | cut -c1-90
drag 0 640 300 1275 400
call dump | grep "win-A" | cut -c1-90
b=$(call dump | grep "win-B" | awk '{ split($4,p,","); split($5,s,"x"); print p[1]+s[1] }')
echo "B right edge $b"
call dump | grep "win-A" | awk -v b="$b" '{ split($4,p,","); split($5,s,"x"); exit !(p[1] >= b - 1 && p[1] + s[1] >= 1279 && s[1] > 400 && s[2] > 799) }' && ok "right edge fills the room beside the float" || bad "fill"
drag 0 1000 300 1275 790
call dump | grep "win-A" | cut -c1-90
pkill -P "$$" -x alacritty
call setFloatFirst false
exit $rc
