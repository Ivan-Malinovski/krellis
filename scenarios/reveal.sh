# Moving focus to an off-screen neighbour slides a zoomed-in frame over at the same size instead of widening it.
# Seeded with spawnTarget=end (windows in opening order) and peek=0 so the camera is the frame itself (with a peek gutter the camera differs mid-row).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
rc=0
check() { [ "$2" = "$3" ] && echo "ok $1: $2" || { echo "FAIL $1: got '$2' want '$3'"; rc=1; }; }
camw() { call dump | sed -nE 's/^camera [-0-9.e]+ [-0-9.e]+ ([-0-9.e]+) .*/\1/p'; }
framed() { call dump | sed -nE "s/^framed '(.*)'/\1/p"; }
for n in A B C D; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
# The last window opened is the active one; frame it alone, then move focus leftwards.
call toggleMaximize 3; sleep 1.5
check "one tile framed" "$(framed)" p4
w0=$(camw)
call focusDirection left; sleep 1.5
check "slid to the neighbour" "$(framed)" p3
check "frame kept its size" "$(camw)" "$w0"
call previousPanel; sleep 1.5
check "cycling slides on" "$(framed)" p2
check "size still kept" "$(camw)" "$w0"
call dump | grep -E "^camera|^framed|^p"
pkill -P "$$" -x alacritty
exit $rc
