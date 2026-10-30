#!/usr/bin/env bash
# Start at a genuinely deep tile; each outward event must be a new wheel gesture.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL deep-out exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET ZOOM_LOAD_APP=kitty ZOOM_LOAD_BLUR=0 ZOOM_LOAD_SINGLE_INSTANCE=1 ZOOM_LOAD_FOCUS_NEW=1
unset DISPLAY
cp "$KRELLIS_ROOT/scenarios/zoom-load-app.bash" "$NEST_WORK/client"
chmod +x "$NEST_WORK/client"
python3 "$KRELLIS_ROOT/scenarios/deep-out.py"
