#!/usr/bin/env bash
# A window that is out of view (off the screen on the canvas, or stashed in an edge drawer) and is activated from
# outside Krellis (the start menu, a launcher, another app asking for focus) must come into view at its proper size,
# exactly as a click on the minimap or the quick-jump palette brings it.
# Seeded with scenarios/pin.rc (spawnTarget=end, peek=0).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'pkill -P "$$" -x alacritty || true' EXIT
for n in A B C D; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
seq=0
activate() {
  seq=$((seq+1))
  cat >"$NEST_WORK/act$seq.js" <<JS
for (const w of workspace.windowList()) if (w.caption === 'win-$1') workspace.activeWindow = w;
JS
  local n; n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/act$seq.js" "act$seq")
  qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null
}
show() { call dump | grep -E "^(ACTIVE|camera|framed|p[0-9])" | sed 's/ app .* drawn/ drawn/'; }
echo "=== off-screen tile"
call toggleMaximize 3; sleep 1.5
echo "--- p4 framed"; show
activate A; sleep 0.25
echo "--- 250ms after activating A"; show
sleep 1.8
echo "--- settled"; show
# the window is on screen, at its tile's size, and framed
x1=$(call dump | sed -nE "s/^p1 .* screen ([-0-9.]+),.*/\1/p"); w1=$(call dump | sed -nE "s/^p1 .* screen [-0-9.]+,[-0-9.]+ ([0-9.]+)x.*/\1/p")
awk -v x="$x1" -v w="$w1" 'BEGIN{exit !(x>=0 && x+w<=1281 && w>1000)}' && echo "PASS offscreen tile came into view at x=$x1 w=$w1" || echo "FAIL offscreen tile x=$x1 w=$w1"
$KRELLIS_ROOT/tools/shot activate-offscreen >/dev/null
echo "=== drawer"
call stashActive; sleep 1.5
echo "--- A stashed"; show
activate D; sleep 1.5
activate A; sleep 0.25
echo "--- 250ms after activating the drawer window"; show
sleep 1.8
echo "--- settled"; show
x1=$(call dump | sed -nE "s/^p1 .* screen ([-0-9.]+),.*/\1/p"); d1=$(call dump | sed -nE "s/^p1 .* drawnpos ([-0-9.]+),.*/\1/p")
awk -v x="$x1" -v d="$d1" 'BEGIN{exit !(x>=0 && x<1280 && d>=0 && d<1280)}' && echo "PASS drawer window slid into view x=$x1 drawn=$d1" || echo "FAIL drawer window still out: x=$x1 drawn=$d1"
$KRELLIS_ROOT/tools/shot activate-drawer >/dev/null
