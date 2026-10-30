#!/usr/bin/env bash
# Frame timings at 25/50/100 windows. Requires the explicit krellis_paint_probe target.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL zoom-load exited at line $LINENO"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
case "${ZOOM_LOAD_APP:-qt}" in
    qt) c++ -O2 -fPIC "$KRELLIS_ROOT/scenarios/zoom-load-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets);;
    kitty|dolphin)
        cp "$KRELLIS_ROOT/scenarios/zoom-load-app.bash" "$NEST_WORK/client"
        chmod +x "$NEST_WORK/client";;
    *) echo "FAIL unknown ZOOM_LOAD_APP"; exit 1;;
esac
python3 "$KRELLIS_ROOT/scenarios/zoom-load.py"
