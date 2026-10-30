#!/usr/bin/env bash
# Hyprland profile: a zoom resizes the client once; its new layout must blend in, not replace the old one in one frame.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL zoom-crossfade exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
c++ -O2 -fPIC "$KRELLIS_ROOT/scenarios/zoom-load-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
python3 "$KRELLIS_ROOT/scenarios/zoom-crossfade.py"
