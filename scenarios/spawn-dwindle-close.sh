# Closing a dwindle window gives its sibling (the window it was split from, or the whole group it was split with) the
# group's entire share, as in Hyprland; the rest of the row keeps its size. Before, the freed share went to the next tile
# in the row. Built with the default dwindleSide=cursor: w2 opens left of p1, w3 on the right half of p2, so the row is
# (p2 | p3) | p1 and closing p3 would have handed its share to p1 instead of p2.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
width() { call dump | sed -nE "s/^$1 .* screen [-0-9.]+,[-0-9.]+ ([0-9.]+)x[0-9.]+.*/\1/p" | head -1; }
rc=0
check() { [ "$2" = "$3" ] && echo "ok $1: $2" || { echo "FAIL $1: got '$2' want '$3'"; rc=1; }; }
near() { # name got want tolerance
    awk -v g="$2" -v w="$3" -v t="$4" 'BEGIN { d = g - w; if (d < 0) d = -d; exit !(d <= t) }' \
        && echo "ok $1: $2 ~ $3" || { echo "FAIL $1: got $2 want $3"; rc=1; }
}
open() { alacritty --title "$1" -e sleep 600 >/dev/null 2>&1 & pid=$!; sleep 2.5; }
sleep 1
call simMotion 100 400; open w1
call simMotion 100 400; open w2 # p1 fills the output; the pointer is in its left half, so w2 goes before it
check left "$(order)" "p2 p1"
call simMotion 1000 400; open w3; w3=$pid # focused p2 is 1188 wide, the pointer is in its right half
check three "$(order)" "p2 p3 p1"
near p2-halved "$(width p2)" 588 6
near p3-halved "$(width p3)" 588 6
near p1-untouched "$(width p1)" 1188 6
kill "$w3"; sleep 2.5
check closed-p3 "$(order)" "p2 p1"
near p2-takes-the-pair "$(width p2)" 1188 6
near p1-keeps-its-width "$(width p1)" 1188 6
pkill -x alacritty
exit $rc
