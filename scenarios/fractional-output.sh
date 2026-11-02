#!/usr/bin/env bash
# NEST_OUTPUTS=2. Painting and input geometry on two fractionally scaled outputs (1.25 and 1.5).
#
# KWin 6.8.2 has no runtime way to change an output's scale (LogicalOutput::scale is a read-only property,
# the scripting Workspace exposes no output configuration, and tools/nested.sh starts the nested KWin with
# a fixed command line), so the scale has to be in the stored output configuration KWin reads at startup.
# scenarios/fractional-output.env puts scenarios/fractional-output-kwin/ (a kwin_wayland wrapper) first on
# PATH; it writes kwinoutputconfig.json with one scale per output and then runs the real binary. Both
# outputs stay 1280x800 *pixels*, so their logical geometry is 1024x640 and 853.333x533.333: the second is
# not an integer number of device pixels, which is the case the developer notes call unverified.
#
# Asserts, per output:
#   - dump's area is pixelSize/scale (bar on: 1024x592 and 853.333x485.333), and the 1.5 one is fractional;
#   - the two painted tiles lie inside that logical rect, keep the layout's 6 px frame margin and 12 px gap,
#     tile the area exactly, and are painted at the logical size - not the 1280 px device size;
#   - paint scale is 1 (drawn == screen, buffer == drawn), so the output scale is not applied twice;
#   - a middle-click on the painted seam between the two tiles flips that split, and the same click at the
#     seam position multiplied by the output scale hits no seam and changes nothing;
#   - a click on the bar's "save" key, found with barHit at the fractional coordinates, starts framing on
#     that output only (dump NAMING 1), and Esc cancels it.
set -euo pipefail
trap 'echo "FAIL fractional-output exited unexpectedly"' ERR
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
python3 "$KRELLIS_ROOT/scenarios/fractional-output.py"
