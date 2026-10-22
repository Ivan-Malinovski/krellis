# A new tiled window opens beside the tile under the cursor: near a tile's edge that edge, in its middle the nearer half of its longer side.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
rc=0
check() { [ "$(order)" = "$2" ] && echo "ok $1: $(order)" || { echo "FAIL $1: got '$(order)' want '$2'"; rc=1; }; }
open() { alacritty --title "$1" -e sleep 600 >/dev/null 2>&1 & sleep 2.5; }
sleep 1
call simMotion 100 100; open w1                                   # empty canvas: appended
call simMotion 1270 790; open w2; check second-appended "p1 p2"   # pointer over no tile: appended
call dump | grep -E "^p[0-9]+ " | sed -E "s/ frame.*//"
call simMotion 20 400; open w3; check left-edge-of-p1 "p3 p1 p2"
call simMotion 1275 400; open w4; check right-edge-of-last "p3 p1 p2 p4"
call simMotion 100 300; open w5                                    # middle of p3, a tall tile: splits its upper half, new window on top
check tall-tile-middle-top "p5 p3 p1 p2 p4"
# stacked, not side by side: same x, p5 above p3
pos() { call dump | awk -v id="$1" '$1 == id { for (i = 1; i <= NF; i++) if ($i == "screen") { print $(i+1); exit } }'; }
p3=$(pos p3); p5=$(pos p5)
[ "${p3%%,*}" = "${p5%%,*}" ] && awk -v a="${p5##*,}" -v b="${p3##*,}" 'BEGIN { exit !(a < b) }' \
    && echo "ok stacked: p5 $p5 above p3 $p3" || { echo "FAIL stacked: p5 $p5 p3 $p3"; rc=1; }
pkill -x alacritty
exit $rc
