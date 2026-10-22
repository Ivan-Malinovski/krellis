#!/usr/bin/env bash
# Run with NEST_OUTPUTS=2. Assert output-local state and real panel struts.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=/home/ivan/dev/krellis
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'pkill -P "$$" -x alacritty || true; kill ${panel:-} 2>/dev/null || true' EXIT
call dump | grep '^OUTPUT 1 '
call toggleBar >/dev/null # the bar is off by default; one call covers every output
for n in A B C D; do
    alacritty --title "monitor-$n" -e sleep 600 >/dev/null 2>&1 &
    sleep 0.6
done
sleep 1
# New windows use KWin's initial output; move each explicitly for reproducibility.
place() {
    local title=$1 dest=$2 source index snapshot
    for source in 0 1; do
        snapshot=$(call dumpOutput "$source")
        index=$(awk -v title="'$title'" '/ screen / { if (index($0,title)) { print n+0; exit } n++ }' <<<"$snapshot")
        if [ -n "$index" ]; then
            call selectOutput "$source"; call moveToOutput "$index" "$dest"; return
        fi
    done
    echo "missing $title"; exit 1
}
place monitor-A 0; place monitor-B 0; place monitor-C 1; place monitor-D 1
for output in 0 1; do call selectOutput "$output"; call overview; done
sleep 1
for output in 0 1; do test "$(call dumpOutput "$output" | grep -c ' screen ')" -eq 2; done
call dump | tee "$NEST_OUT/initial.txt"
call dumpOutput 0 | grep -Fx 'area 0,0 1280x752 bar 0,752 1280x48'
call dumpOutput 1 | grep -Fx 'area 1280,0 1280x752 bar 1280,752 1280x48'
right=$(call dumpOutput 1 | head -2)
call selectOutput 0; call focusIndex 1; sleep 1
left=$(call dumpOutput 0 | head -2)
test "$right" = "$(call dumpOutput 1 | head -2)"
# Screenshot also catches off-canvas windows bleeding onto the other monitor.
"$project/tools/shot" monitors-focus
call simPinch 1.3 1920 400; sleep 0.3
call dumpOutput 1 | tee "$NEST_OUT/right-gesture.txt"
test "$left" = "$(call dumpOutput 0 | head -2)"
test "$right" != "$(call dumpOutput 1 | head -2)"
call simPinchEnd; sleep 1
# Each bar owns its own saved framings and minimap.
clickKey() {
    local output=$1 wanted=$2 x key
    for x in $(seq $((output*1280+6)) 8 $((output*1280+1278))); do
        key=$(call barHit "$x" 776)
        if [ "$key" = "$wanted" ]; then
            call simButton "$x" 776 true 0; call simButton "$x" 776 false 0; return
        fi
    done
    echo "missing bar action $wanted on $output"; exit 1
}
clickKey 0 save
call simText "Left monitor"; call simKey 16777220 true; call simKey 16777220 false
clickKey 1 map
sleep 0.5
call dumpOutput 1 | grep '^MAP .*shown 1 '
! call dumpOutput 0 | grep -q '^MAP '
# Output 1 has no saved view chip; output 0 has one.
for output in 0 1; do
    framings=$(for x in $(seq $((output*1280+6)) 8 $((output*1280+800))); do call barHit "$x" 776; done | grep '^framing:' || true)
    if [ "$output" = 0 ]; then test -n "$framings"; else test -z "$framings"; fi
done
for output in 0 1; do call selectOutput "$output"; call overview; done
sleep 1
# Hiding a left window must leave right layout and camera alone.
right=$(call dumpOutput 1)
call selectOutput 0; call toggleHide 0; sleep 0.5
test "$right" = "$(call dumpOutput 1)"
call selectOutput 0; call toggleHide 0; sleep 0.6
# A real exclusive-zone panel on output 1 only.
c++ -fPIC "$project/scenarios/panel.cpp" -o "$NEST_WORK/panel" $(pkg-config --cflags --libs Qt6Gui) -lLayerShellQtInterface
"$NEST_WORK/panel" top 1 >"$NEST_OUT/panel.log" 2>&1 & panel=$!
sleep 1
call dumpOutput 0 | grep -Fx 'area 0,0 1280x752 bar 0,752 1280x48'
call dumpOutput 1 | grep -Fx 'area 1280,64 1280x688 bar 1280,752 1280x48'
test "$(call dumpOutput 1 | grep -c ' screen ')" -eq 2
"$project/tools/shot" monitors-panel
kill "$panel"; wait "$panel" || true; panel=
sleep 1
# Rotate only the second output: the nested backend permits rotation but
# deliberately ignores requested scale changes (host fractional scale owns it).
left=$(call dumpOutput 0 | head -2)
kscreen-doctor output.WL-1.rotation.left >"$NEST_OUT/rotate.txt" 2>&1
sleep 1
call dumpOutput 1 | tee "$NEST_OUT/rotated.txt" | grep -Fx 'area 1280,0 800x1232 bar 1280,1232 800x48'
test "$left" = "$(call dumpOutput 0 | head -2)"
"$project/tools/shot" monitors-rotated
kscreen-doctor output.WL-1.rotation.none >"$NEST_OUT/unrotate.txt" 2>&1
sleep 1
# Cross-output drag commits into the destination canvas, then cancel stays put.
call selectOutput 0
call simDragStart 0 200 200 | grep true
call simDragMove 400 250; sleep 0.2
call simDragMove 1650 300; sleep 0.2
call simDragEnd true
call dumpOutput 1 | tee "$NEST_OUT/transfer.txt" | grep ' FLY '
sleep 1
test "$(call dumpOutput 0 | grep -c ' screen ')" -eq 1
test "$(call dumpOutput 1 | grep -c ' screen ')" -eq 3
call selectOutput 1
call simDragStart 0 1600 200 | grep true
call simDragMove 1800 250; call simDragMove 300 300
call simDragEnd false; sleep 1
test "$(call dumpOutput 0 | grep -c ' screen ')" -eq 1
test "$(call dumpOutput 1 | grep -c ' screen ')" -eq 3
call dump | tee "$NEST_OUT/final.txt"
"$project/tools/shot" monitors-final
# Disable/re-enable only this nested compositor's output, exercising hotplug.
kscreen-doctor -o >"$NEST_OUT/outputs.txt"
kscreen-doctor output.WL-1.disable >"$NEST_OUT/disable.txt" 2>&1
sleep 1
test "$(call dump | grep -c ' screen ')" -eq 4
! call dump | grep -q '^OUTPUT 1 '
kscreen-doctor output.WL-1.enable >"$NEST_OUT/enable.txt" 2>&1
sleep 1
call dump | grep '^OUTPUT 1 '
test "$(call dump | grep -c ' screen ')" -eq 4
echo 'PASS independent canvases, gesture, hide, struts, transfer, cancel, hotplug'
