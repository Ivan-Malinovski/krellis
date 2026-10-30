#!/usr/bin/env bash
# Real-app companion to zoom-load.sh; private test sockets and empty directories only.
set -euo pipefail
[[ "$NEST_SOCKET" == krellis-test-* && "$WAYLAND_DISPLAY" == "$NEST_SOCKET"
    && "$XDG_CONFIG_HOME" == "$NEST_WORK/config" ]]
pids=()
cleanup() { if [ ${#pids[@]} -gt 0 ]; then kill "${pids[@]}" 2>/dev/null || true; wait 2>/dev/null || true; fi; }
trap cleanup EXIT
trap 'exit 0' TERM INT
mkdir -p "$NEST_WORK/demo"
kitty_options=()
if [[ "${ZOOM_LOAD_BLUR:-0}" == 1 ]]; then
    kitty_options=(-o background_opacity=0.8 -o background_blur=16)
fi
# Independent controls let depth tests distinguish blending from blur costs.
if [[ -n "${ZOOM_LOAD_OPACITY:-}" ]]; then
    kitty_options+=(-o "background_opacity=$ZOOM_LOAD_OPACITY")
fi
if [[ -n "${ZOOM_LOAD_BLUR_RADIUS:-}" ]]; then
    kitty_options+=(-o "background_blur=$ZOOM_LOAD_BLUR_RADIUS")
fi
kitty_command=(/usr/bin/sleep 600)
if [[ "${ZOOM_LOAD_CONTENT:-empty}" == grid ]]; then
    kitty_command=(/usr/bin/python3 "$KRELLIS_ROOT/scenarios/zoom-load-grid.py")
fi
kitty_instance_args=()
if [[ "${ZOOM_LOAD_SINGLE_INSTANCE:-0}" == 1 ]]; then
    # Never join the user's Kitty process: the group is unique to this test socket.
    kitty_instance_args=(--single-instance --instance-group "$NEST_SOCKET")
fi
for ((i=0; i<$1; i++)); do
    case "$ZOOM_LOAD_APP" in
        kitty) kitty --config NONE "${kitty_instance_args[@]}" "${kitty_options[@]}" --class krellis-zoom-load --title "zoom-load-$i" --directory "$NEST_WORK/demo" "${kitty_command[@]}" >/dev/null 2>&1 &;;
        dolphin) dolphin --new-window "$NEST_WORK/demo" >/dev/null 2>&1 &;;
        *) exit 1;;
    esac
    pids+=($!)
    if [[ "${ZOOM_LOAD_FOCUS_NEW:-0}" == 1 ]]; then
        found=0
        for ((attempt=0; attempt<100; ++attempt)); do
            if qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.dump | rg -q "'zoom-load-$i' screen"; then found=1; break; fi
            sleep .05
        done
        [[ "$found" == 1 ]]
        printf "for (const w of workspace.windowList()) if (w.caption === 'zoom-load-%d') workspace.activeWindow = w;\n" "$i" > "$NEST_WORK/focus.js"
        script_name="zoom-load-focus-$NEST_SOCKET-$i"
        script_id=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/focus.js" "$script_name")
        qdbus6 org.kde.KWin "/Scripting/Script$script_id" org.kde.kwin.Script.run >/dev/null
        qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$script_name" >/dev/null
    fi
    sleep .1
done
wait
