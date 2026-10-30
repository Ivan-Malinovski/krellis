# spawnTarget=dwindle with the default dwindleSide=cursor: the new window takes the half of the focused tile the
# pointer is in (Hyprland's force_split=0). A pointer outside the tile counts as the nearer half. On a 2400x800 output a
# lone window is wide, so the halves are left/right; a 588x788 tile is tall, so they are top/bottom. Then
# dwindleSide=after puts the window right of / below the tile whatever the pointer does.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
screen() { call dump | sed -nE "s/^$1 .* screen ([-0-9.]+),([-0-9.]+) ([0-9.]+)x([0-9.]+).*/\\$2/p" | head -1; }
rc=0
check() { [ "$2" = "$3" ] && echo "ok $1: $2" || { echo "FAIL $1: got '$2' want '$3'"; rc=1; }; }
below() { awk -v a="$2" -v b="$3" 'BEGIN { exit !(a > b) }' && echo "ok $1: $2 > $3" || { echo "FAIL $1: $2 not below $3"; rc=1; }; }
open() { alacritty --title "$1" -e sleep 600 >/dev/null 2>&1 & sleep 2.5; }
sleep 1
open w1
call simMotion 100 400; open w2
check left-half-opens-before "$(order)" "p2 p1"
call simMotion 2000 400; open w3 # focused p2 is the left half; the pointer is beyond it: the nearer (right) half
check outside-tile-is-nearer-half "$(order)" "p2 p3 p1"
call simMotion 300 100; open w4 # focused p3 is 588x788, tall: the halves are top/bottom, the pointer is in the top one
check top-half "$(order)" "p2 p4 p3 p1"
below p3-under-p4 "$(screen p3 2)" "$(screen p4 2)"
call simMotion 300 700; open w5 # focused p4 is wide again after the split (588x388): pointer left of its middle
check wide-again "$(order)" "p2 p5 p4 p3 p1"
sed -i 's/^spawnTarget=dwindle/dwindleSide=after\nspawnTarget=dwindle/' "$XDG_CONFIG_HOME/krellisrc"
call reloadSettings; sleep 0.5
call simMotion 100 100; open w6 # focused p5 is 288x388: tall, so below it, although the pointer is in its top half
check after-ignores-pointer "$(order)" "p2 p5 p6 p4 p3 p1"
pkill -x alacritty
exit $rc
