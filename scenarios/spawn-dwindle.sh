# spawnTarget=dwindle: a new tiled window halves the focused tile instead of joining its row. On a 2400x800 output two
# windows are 1200x800 each (1188 drawn, after the 6 px gaps), so the focused one splits on the right; spawnTarget=focused would add a third
# equal column (800 each) and shrink the first window as well. Run with NEST_KRELLISRC=scenarios/spawn-dwindle.rc.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
width() { call dump | sed -nE "s/^$1 .* screen [-0-9.]+,[-0-9.]+ ([0-9.]+)x[0-9.]+.*/\1/p" | head -1; }
rc=0
check() { [ "$2" = "$3" ] && echo "ok $1: $2" || { echo "FAIL $1: got '$2' want '$3'"; rc=1; }; }
open() { alacritty --title "$1" -e sleep 600 >/dev/null 2>&1 & sleep 2.5; }
near() { # name got want tolerance
    awk -v g="$2" -v w="$3" -v t="$4" 'BEGIN { d = g - w; if (d < 0) d = -d; exit !(d <= t) }' \
        && echo "ok $1: $2 ~ $3" || { echo "FAIL $1: got $2 want $3"; rc=1; }
}
sleep 1
call simMotion 100 400; open w1
call simMotion 100 400; open w2
check two-in-a-row "$(order)" "p1 p2"
near p1-half "$(width p1)" 1188 6
call simMotion 100 400; open w3 # the pointer is over p1; the focused window is p2
check order "$(order)" "p1 p2 p3"
near p1-keeps-its-width "$(width p1)" 1188 6
near p2-halved "$(width p2)" 588 6
near p3-halved "$(width p3)" 588 6
pkill -x alacritty
exit $rc
