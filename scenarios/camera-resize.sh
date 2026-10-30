#!/usr/bin/env bash
# NEST_OUTPUTS=2. Navigation transforms retained buffers and only configures visible tiles after settling.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL camera-resize exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
c++ -O2 -fPIC "$KRELLIS_ROOT/scenarios/zoom-load-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
c++ -std=c++20 -fPIC "$KRELLIS_ROOT/scenarios/pointer-driver.cpp" -o "$NEST_WORK/pointer-driver" $(pkg-config --cflags --libs KWaylandClient wayland-client)
python3 "$KRELLIS_ROOT/scenarios/camera-resize.py"
