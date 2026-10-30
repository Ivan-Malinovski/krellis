#!/usr/bin/env bash
# NEST_OUTPUTS=2. Output 0 is rotated to portrait, output 1 stays landscape. A window carried to an output of the
# opposite orientation must never be drawn at a different aspect than its client's frame while it flies there and
# while a slow client lays out (the Hyprland profile used to scale the old buffer straight into the new tile:
# landscape to portrait drew a window 2x too tall). The window left behind keeps the profile's plain scaling.
set -uo pipefail
[[ "$NEST_SOCKET" == krellis-test-* ]]
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'pkill -P "$$" -x slow || true' EXIT
test "$NEST_OUTPUTS" -eq 2
kscreen-doctor output.Virtual-0.rotation.right >/dev/null 2>&1; sleep 2
c++ -fPIC $KRELLIS_ROOT/scenarios/slow-client.cpp -o "$NEST_WORK/slow" $(pkg-config --cflags --libs Qt6Widgets)
for n in A B; do "$NEST_WORK/slow" "win-$n" >/dev/null 2>&1 & sleep 1.5; done
move() {
    local title=$1 dest=$2 source snapshot index
    for source in 0 1; do
        snapshot=$(call dumpOutput "$source")
        index=$(awk -v title="'$title'" '/ screen / { if (index($0,title)) { print n+0; exit } n++ }' <<<"$snapshot")
        if [ -n "$index" ]; then call selectOutput "$source"; call moveToOutput "$index" "$dest"; return; fi
    done
    echo "missing $title"; exit 1
}
move win-A 1; move win-B 1; sleep 3.5 # longer than CrossFitMs, so the window left behind is no longer 'recently crossed'
rm -f "$NEST_OUT/samples.txt"
sample() { # phase
    for i in $(seq 1 40); do
        call dump | grep -E "^p[0-9]+ " | sed -E 's/^p[0-9]+ (\x27[^\x27]*\x27).* frame [^ ]+ ([0-9.]+)x([0-9.]+) .* drawn ([0-9.]+)x([0-9.]+) .*/\1 \2 \3 \4 \5/' | sed "s/^/$1 /" | tee -a "$NEST_OUT/samples.txt"
        sleep 0.05
    done
}
move win-B 0; sample landscape-to-portrait; sleep 2
move win-B 1; sample portrait-to-landscape; sleep 1
rc=0
# stretch = (drawn aspect) / (frame aspect); 1.00 is undistorted
worst() { awk -v ph="$1" -v nm="'$2'" '$1==ph && $2==nm && $3>0 && $4>0 && $6>0 { s=($5/$6)/($3/$4); if (s<1) s=1/s; if (s>m) m=s } END { printf "%.2f", m+0 }' "$NEST_OUT/samples.txt"; }
for phase in landscape-to-portrait portrait-to-landscape; do
    moved=$(worst $phase win-B); left=$(worst $phase win-A)
    echo "$phase: moved window worst stretch ${moved}x, window left behind ${left}x"
    awk -v v="$moved" 'BEGIN { exit !(v <= 1.03) }' && echo "PASS $phase moved window is never stretched" || { echo "FAIL $phase moved window stretched ${moved}x"; rc=1; }
done
left=$(worst landscape-to-portrait win-A)
awk -v v="$left" 'BEGIN { exit !(v >= 1.2) }' && echo "PASS the window left behind keeps the profile's scaling" || { echo "FAIL the window left behind no longer scales (${left}x)"; rc=1; }
exit $rc
