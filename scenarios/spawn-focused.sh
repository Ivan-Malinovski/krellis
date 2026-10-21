# spawnTarget=focused: a new tiled window opens beside the focused window, wherever the pointer is. Run with
#   NEST_TRELLISRC=<file with spawnTarget=focused> (the check below reads the log line to prove the key was seen).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
pos() { call dump | sed -nE "s/^$1 .* screen ([-0-9.]+),([-0-9.]+) .*/\1 \2/p" | head -1; }
rc=0
check() { [ "$2" = "$3" ] && echo "ok $1: $2" || { echo "FAIL $1: got '$2' want '$3'"; rc=1; }; }
open() { alacritty --title "$1" -e sleep 600 >/dev/null 2>&1 & sleep 2.5; }
sleep 1
call simMotion 100 400; open w1
# One wide tile, pointer on its left half: the pointer mode would open on the left ("p2 p1").
call simMotion 100 400; open w2; check wide-tile-right-of-focused "$(order)" "p1 p2"
# w2 is active and tall now; the pointer sits over p1, but the window opens below the focused p2.
call simMotion 100 400; open w3
check order-unchanged-for-stack "$(order)" "p1 p2 p3"
set -- $(pos p2); x2=$1; y2=$2
set -- $(pos p3); x3=$1; y3=$2
[ "${x2%.*}" = "${x3%.*}" ] && [ "${y3%.*}" -gt "${y2%.*}" ] && echo "ok p3 stacked below p2 ($x3,$y3 vs $x2,$y2)" || { echo "FAIL p3 at $x3,$y3, p2 at $x2,$y2"; rc=1; }
pkill -x alacritty
exit $rc
