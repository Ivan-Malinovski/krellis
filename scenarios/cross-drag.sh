#!/usr/bin/env bash
# Run with NEST_OUTPUTS=2. A real title-bar drag that crosses to the other output must hand the live drag
# to that output's canvas (drop slot, reflow preview), and a drop there must commit.
set -euo pipefail
[[ "$NEST_SOCKET" == krellis-test-* ]]
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done; kill ${driver:-} 2>/dev/null || true' EXIT
test "$NEST_OUTPUTS" -eq 2
c++ -std=c++20 -fPIC "$project/scenarios/pointer-driver.cpp" -o "$NEST_WORK/pointer-driver" $(pkg-config --cflags --libs KWaylandClient wayland-client)
c++ -fPIC "$project/scenarios/damage-client.cpp" -o "$NEST_WORK/damage-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for specification in 'alpha #d23c41' 'beta #35bd69' 'gamma #3699de'; do
    read -r name color <<<"$specification"
    "$NEST_WORK/damage-client" "xdrag-$name" "$color" >"$NEST_OUT/client-$name.txt" 2>&1 & clients="$clients $!"
    sleep 0.3
done
sleep 1.5
place() {
    local app=$1 destination=$2 source index
    for source in 0 1; do
        index=$(call dumpOutput "$source" | awk -v app="'xdrag-$app'" '/ screen / { if (index($0,app)) { print n+0; exit } n++ }')
        if [ -n "$index" ]; then
            [ "$source" = "$destination" ] && return
            call selectOutput "$source"; call moveToOutput "$index" "$destination"; return
        fi
    done
    echo "missing $app"; exit 1
}
place alpha 0; place beta 0; place gamma 1
sleep 1.2
count() { call dumpOutput "$1" | grep -c ' screen ' || true; }
echo "before: out0=$(count 0) out1=$(count 1)"
test "$(count 0)" -eq 2; test "$(count 1)" -eq 1
fifo=$NEST_WORK/driver.in
rm -f "$fifo"; mkfifo "$fifo"
"$NEST_WORK/pointer-driver" <"$fifo" >"$NEST_OUT/driver.out" 2>&1 & driver=$!
exec 9>"$fifo"
send() { echo "$1" >&9; sleep 0.06; }
sleep 0.8
title=$(call dumpOutput 0 | awk "/'xdrag-alpha'/ { match(\$0,/screen ([-0-9.]+),([-0-9.]+)/,a); print int(a[1]+60), int(a[2]+14) }")
echo "press at $title"
send "move $title"; send down
for x in 250 500 800 1100 1250 1290 1400 1600 1800; do send "move $x 300"; done
sleep 0.5
echo "--- mid drag"; call dumpOutput 0 | grep -E 'DRAG|SLOT' || true; call dumpOutput 1 | grep -E 'DRAG|SLOT' || true
call dumpOutput 1 | grep -q 'DRAG' || { echo "FAIL destination has no live drag"; exit 1; }
lift=$(call dumpOutput 1 | awk '/^DRAG lifted/ { split($3,a,","); print int(a[1]), int(a[2]), int($4) }')
echo "lifted card (x y w): $lift"
read -r lx ly lw <<<"$lift"
test "$lx" -le 520 && test "$((lx + lw))" -ge 520 || { echo "FAIL card is not under the pointer"; exit 1; }
call dumpOutput 0 | grep -q 'DRAG' && { echo "FAIL source still dragging"; exit 1; }
"$project/tools/shot" cross-drag >/dev/null || true
send up
sleep 1.5
echo "after: out0=$(count 0) out1=$(count 1)"
test "$(count 0)" -eq 1; test "$(count 1)" -eq 2
echo "PASS live cross-output drag"
