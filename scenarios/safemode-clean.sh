#!/usr/bin/env bash
# Unloading the effect normally marks the boot marker clean and resets its strikes. Seed: NEST_BOOT_STRIKES=0.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
marker="$NEST_WORK/state/krellis/boot.json"
sleep 2
echo "before: $(cat "$marker")"
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect krellis >/dev/null; sleep 1.5
echo "after:  $(cat "$marker")"
grep -q '"cleanExit":true' "$marker" && grep -q '"strikes":0' "$marker" && echo "PASS clean unload resets the marker" || echo "FAIL clean unload resets the marker"
