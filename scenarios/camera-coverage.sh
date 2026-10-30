#!/usr/bin/env bash
# Real-frame pixels inside retained tiles, and beside a cropped topmost window.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL camera-coverage exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
c++ -O2 -fPIC "$KRELLIS_ROOT/scenarios/zoom-load-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
c++ -std=c++20 -fPIC "$KRELLIS_ROOT/scenarios/pointer-driver.cpp" -o "$NEST_WORK/pointer-driver" $(pkg-config --cflags --libs KWaylandClient wayland-client)
python3 "$KRELLIS_ROOT/scenarios/camera-coverage.py"
