#!/usr/bin/env bash
# Open an isolated interactive Trellis desktop. No live effect installation.
set -euo pipefail
project=$(cd -- "$(dirname -- "$0")/.." && pwd)
preview_socket=${NEST_SOCKET:-trellis-nest}
if [ -S "${XDG_RUNTIME_DIR:?}/$preview_socket" ]; then
    echo 'A nested Trellis session is already running. Close it before starting the preview.' >&2
    exit 1
fi
if [ ! -f "$project/build/root/kwin/effects/plugins/trellis.so" ]; then
    echo 'Build the trellis target first.' >&2
    exit 1
fi
# Keep Qt Quick's offscreen UI off the failing nested OpenGL framebuffer path.
# The compositor and terminal clients retain their normal renderer.
export QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software}
export NEST_WORK
NEST_WORK=$(mktemp -d /tmp/trellis-preview.XXXXXX)
echo 'Opening Trellis in a separate desktop window (software-rendered Qt Quick UI).'
echo 'Ctrl+Alt+drag pans; Ctrl+Alt+V+drag zooms. Use the Overview bar button.'
echo 'The terminals include t commands for focus, float, hide and native resize.'
echo "Preview files: $NEST_WORK"
exec "$project/tools/nested.sh" "$project/build/root" trellis "$project/scenarios/preview.sh" 1600 1000
