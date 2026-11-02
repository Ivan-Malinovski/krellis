#!/usr/bin/env bash
# A native resize of a float tracks the pointer 1:1 at every camera zoom.
#
# effect/native_resize.cpp applies the native edge delta straight to the painted float rect ("applying the
# paint scale again would make an already scaled window lag behind the pointer"), while every other canvas
# edit works in world units. Nothing tested that. This drives a real KWin interactive resize
# (simNativeResizeStart/Move/End) of one of two floats while the second float must not move, and asserts, for
# a pointer move of dx:
#   - the painted right edge moves by exactly dx logical pixels and the painted width grows by dx, at
#     camera 1.0 (control), zoomed in onto the docked tile (camera 0.516x1.0) and zoomed out by a pinch
#     (camera 1.515x1.515);
#   - the painted left edge does not move, the preview stays inside the output (so the numbers are not a
#     clamp artefact) and dump's drawn size matches the painted float;
#   - the stored canvas-normalised floating rect moves by dx / output width and its origin does not move:
#     exactly one of the two stored floats grows, and the other one is untouched;
#   - a cancelled preview (saveSession false, then End false) restores both the painted and the stored rect.
# The seed's minWidth/minHeight rule makes the float's native frame 1000x2558 for a 246x630 paint, i.e. the
# "already scaled window" the comment is about. Overlay floats are screen space, so the camera zoom does not
# move or rescale them - that is asserted too, and it is why the painted delta must equal the pointer delta.
set -euo pipefail
trap 'echo "FAIL float-zoom-resize exited unexpectedly"' ERR
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
python3 "$KRELLIS_ROOT/scenarios/float-zoom-resize.py"
