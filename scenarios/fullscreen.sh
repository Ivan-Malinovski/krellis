#!/usr/bin/env bash
# A tiled window that goes fullscreen later (a video in Brave) covers the whole output, drawn untransformed, and gets its tile back.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
row() { call dump | grep "^$1 " | head -1; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
echo "--- tiled"; call dump | grep -E "^camera|^p[0-9]"
before=$(row p2)
setfull() {
cat >"$NEST_WORK/full.js" <<JS
for (const w of workspace.windowList()) if (w.caption === 'win-B') w.fullScreen = $1;
JS
n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/full.js" full$1$RANDOM)
qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null
}
setfull true
sleep 2
echo "--- fullscreen"; call dump | grep -E "^camera|^p[0-9]"
sleep 2; "$KRELLIS_ROOT/tools/shot" fullscreen >/dev/null 2>&1 || true
# Frame and drawn size must both be the output (1280x800).
after=$(row p2)
echo "$after" | grep -q "frame 0.0,0.0 1280[.0]*x800" && echo "PASS B's frame is the output" || echo "FAIL B's frame is not the output: $after"
# The old bug: the frame was right, but the effect still painted it scaled into its tile.
echo "$after" | grep -q " drawn " && echo "FAIL B is still painted through the canvas: $after" || echo "PASS B is painted untransformed"
setfull false
sleep 2
echo "--- restored"; call dump | grep -E "^camera|^p[0-9]"
check "B has its tile back" "$(row p2)" "$before"
"$KRELLIS_ROOT/tools/shot" restored >/dev/null 2>&1 || true
pkill -P "$$" -x alacritty
