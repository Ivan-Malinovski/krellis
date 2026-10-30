#!/usr/bin/env bash
# A window opened during a drag (Brave detaching a tab) is dragged by the compositor from the first moment. Its tile
# is wherever the layout put it, usually not under the pointer, and the lifted card used to keep that offset and trail
# far from the mouse. The card must stay under the pointer for the whole drag.
set -uo pipefail
[[ "$NEST_SOCKET" == krellis-test-* ]]
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'kill ${client:-} ${driver:-} 2>/dev/null || true' EXIT
c++ -std=c++20 -fPIC "$project/scenarios/pointer-driver.cpp" -o "$NEST_WORK/pointer-driver" $(pkg-config --cflags --libs KWaylandClient wayland-client)
c++ -fPIC "$project/scenarios/tearoff-client.cpp" -o "$NEST_WORK/tearoff-client" $(pkg-config --cflags --libs Qt6Widgets)
"$NEST_WORK/tearoff-client" >"$NEST_OUT/client.txt" 2>&1 & client=$!
sleep 2
fifo=$NEST_WORK/driver.in
rm -f "$fifo"; mkfifo "$fifo"
"$NEST_WORK/pointer-driver" <"$fifo" >"$NEST_OUT/driver.out" 2>&1 & driver=$!
exec 9>"$fifo"
send() { echo "$1" >&9; sleep 0.08; }
sleep 0.8
rc=0
send "move 150 400"; send down
sleep 0.8
call dump | cut -c1-160 | head -20
for step in "230 410" "330 430" "450 470" "600 520" "760 560"; do
    send "move $step"; sleep 0.25
    read -r px py <<<"$step"
    lift=$(call dump | awk '/^DRAG lifted/ { split($3,a,","); split($4,b,"x"); print int(a[1]), int(a[2]), int(b[1]), int(b[2]); exit }')
    echo "pointer $px,$py lifted(x y w h): ${lift:-none}"
    if [ -z "$lift" ]; then echo "FAIL no live drag at $step"; rc=1; continue; fi
    read -r lx ly lw lh <<<"$lift"
    if [ "$px" -ge "$lx" ] && [ "$px" -le "$((lx + lw))" ] && [ "$py" -ge "$ly" ] && [ "$py" -le "$((ly + lh))" ]; then
        echo "PASS card under the pointer at $step"
    else
        echo "FAIL card is away from the pointer at $step (offset $((px - lx)),$((py - ly)))"; rc=1
    fi
done
send up
exit $rc
