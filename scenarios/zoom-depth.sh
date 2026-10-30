#!/usr/bin/env bash
# Repeated inward/outward wheel navigation with real transparent, blurred Kitty clients.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL zoom-depth exited at line $LINENO"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET ZOOM_LOAD_APP=kitty ZOOM_LOAD_BLUR=${ZOOM_LOAD_BLUR:-1}
unset DISPLAY
cp "$KRELLIS_ROOT/scenarios/zoom-load-app.bash" "$NEST_WORK/client"
chmod +x "$NEST_WORK/client"
python3 "$KRELLIS_ROOT/scenarios/zoom-depth.py"
