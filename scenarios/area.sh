#!/usr/bin/env bash
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
c++ -fPIC "$project/scenarios/panel.cpp" -o "$NEST_WORK/panel" $(pkg-config --cflags --libs Qt6Gui) -lLayerShellQtInterface
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
alacritty --title area -e sleep 600 >/dev/null 2>&1 &
terminal=$!
trap 'kill "$terminal" ${panel:-} 2>/dev/null || true' EXIT
sleep 2
call toggleBar >/dev/null # the bar is off by default
sleep 0.5
call dump | tee "$NEST_OUT/before.txt" | grep '^area 0,0 1280x752 bar 0,752 1280x48$'
for edge in top bottom; do
    "$NEST_WORK/panel" "$edge" >"$NEST_OUT/panel-$edge.log" 2>&1 & panel=$!
    if [ "$edge" = top ]; then expected='area 0,64 1280x688 bar 0,752 1280x48'; else expected='area 0,0 1280x688 bar 0,688 1280x48'; fi
    for attempt in $(seq 40); do
        snapshot=$(call dump)
        if grep -Fxq "$expected" <<<"$snapshot"; then break; fi
        sleep 0.05
    done
    grep -Fx "$expected" <<<"$snapshot"
    grep ' FLY ' <<<"$snapshot"
    sleep 1
    call dump | tee "$NEST_OUT/$edge.txt" | grep -Fx "$expected"
    # Shell surfaces must remain outside the tile tree.
    test "$(grep -c ' screen ' "$NEST_OUT/$edge.txt")" -eq 1
    "$project/tools/shot" "area-$edge"
    kill "$panel"; wait "$panel" || true; panel=
    sleep 1
    call dump | grep '^area 0,0 1280x752 bar 0,752 1280x48$'
done
