# A float dropped at a screen edge snaps to that half; at a corner, to that quarter (as vibetiles does).
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 1.5; done
sleep 1
rc=0
snap() { call dump | awk '/^SNAP/ { print $2, $3; exit }'; }
# Shift (simulated) makes the drag a float, which is when the screen edge is free to snap rather than dock.
call simForceFloat 1
try() {  # name grabX grabY dropX dropY want
    call simDragStart 1 "$2" "$3" | grep -q true || { echo "FAIL $1: could not grab"; rc=1; return; }
    call simDragMove "$4" "$5"; sleep 0.05; call simDragMove "$4" "$5"; sleep 0.5
    local got; got=$(snap)
    [ "$got" = "$6" ] && echo "ok $1 preview: $got" || { echo "FAIL $1 preview: got '$got' want '$6'"; rc=1; }
    call dump | grep -E "^(SNAP|SLOT)"
    call simDragEnd true; sleep 1.3
    call dump | grep -E "win-B|FLOAT" | head -3
}
try right-half 640 400 1277 400 "640,0 640x800"
try top-left-quarter 900 400 15 20 "0,0 640x400"
try bottom-right-quarter 300 200 1270 790 "640,400 640x400"
try top-half 700 300 640 4 "0,0 1280x400"
# Mid-screen is not a snap zone.
call simDragStart 1 700 300 | grep -q true || { echo "FAIL could not grab"; rc=1; }
call simDragMove 600 400; sleep 0.05; call simDragMove 600 400; sleep 0.4
[ -z "$(snap)" ] && echo "ok no snap mid-screen" || { echo "FAIL snap mid-screen: $(snap)"; rc=1; }
call simDragEnd false; sleep 1
call simForceFloat -1
pkill -P "$$" -x alacritty
exit $rc
