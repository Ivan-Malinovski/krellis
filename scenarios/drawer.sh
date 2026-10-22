#!/usr/bin/env bash
# An edge tile resized to a sliver collapses into a drawer: it rests past the screen edge with a sliver in view,
# slides out under the pointer and back when the pointer leaves. The neighbours reflow into its place.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 0.8
call dump | grep -E "^area|ORDER"
echo "--- squeeze the right-most tile from its inner edge"
call simNativeResizeStart 2 0 0.5 | grep -x true || echo "FAIL no native resize"
call dump | grep RESIZE
call simNativeResizeMove 600 0 | grep -x true; sleep 0.3
call dump | grep -E "RESIZE"
call simNativeResizeEnd true | grep -x true; sleep 1.2
call dump | grep -E "DRAWER|ORDER|p[0-9]"
call dump | grep -q "DRAWER .* closed" && echo "PASS collapsed into a closed drawer" || echo "FAIL no drawer"
call dump | grep ORDER | grep -qE "ORDER .*p3" && echo "FAIL the tile is still tiled" || echo "PASS the neighbours took its place"
$KRELLIS_ROOT/tools/shot drawer-closed >/dev/null
echo "--- hover the sliver"
call dump | grep DRAWER
x=$(call dump | sed -n 's/^DRAWER .* at \([0-9.]*\),.*/\1/p' | head -1); y=$(call dump | sed -n 's/^DRAWER .* at [0-9.]*,\([0-9.]*\) .*/\1/p' | head -1)
echo "sliver at $x,$y"
call simMotion $(awk -v x="$x" 'BEGIN{printf "%d", x+4}') $(awk -v y="$y" 'BEGIN{printf "%d", y+200}') >/dev/null; sleep 1.0
call dump | grep DRAWER
call dump | grep -q "DRAWER .* open" && echo "PASS slides out under the pointer" || echo "FAIL did not open"
$KRELLIS_ROOT/tools/shot drawer-open >/dev/null
echo "--- leave"
call simMotion 300 300 >/dev/null; sleep 1.0
call dump | grep DRAWER
call dump | grep -q "DRAWER .* closed" && echo "PASS slides back" || echo "FAIL stayed open"
pkill -P "$$" -x alacritty
