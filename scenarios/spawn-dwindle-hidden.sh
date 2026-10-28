# Dwindle splits what is on screen: with the focused window out of view (the camera framed another tile), a new window
# halves the visible tile, not the hidden focused one. Two windows, p2 focused; frame p1 so p2 is out of focus (peek);
# a third must dwindle off p1 (order p1 p3 p2), where splitting the hidden p2 would give p1 p2 p3.
# Seeded with spawn-dwindle-hidden.rc.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
rc=0
check() { [ "$2" = "$3" ] && echo "ok $1: $2" || { echo "FAIL $1: got '$2' want '$3'"; rc=1; }; }
open() { alacritty --title "$1" -e sleep 600 >/dev/null 2>&1 & sleep 2.5; }
sleep 1
open w1
open w2
check two "$(order)" "p1 p2"
call toggleMaximize 0; sleep 1.5
check framed-p1 "$(call dump | grep -E '^framed')" "framed 'p1'"
check active-is-hidden-p2 "$(call dump | sed -n 's/^ACTIVE //p')" "p2"
open w3
check splits-the-visible-tile "$(order)" "p1 p3 p2"
pkill -x alacritty
exit $rc
