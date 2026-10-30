#!/usr/bin/env bash
# Rapid inward/outward wheel bursts with real Kitty clients; opt-in GPU timeline queries.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL zoom-burst exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET ZOOM_LOAD_APP=kitty ZOOM_LOAD_BLUR=${ZOOM_LOAD_BLUR:-0}
unset DISPLAY
cp "$KRELLIS_ROOT/scenarios/zoom-load-app.bash" "$NEST_WORK/client"
chmod +x "$NEST_WORK/client"
python3 "$KRELLIS_ROOT/scenarios/zoom-burst.py"
