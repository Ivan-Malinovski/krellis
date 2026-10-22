# Minimap: toggle from the bar, click a tile to frame it, click the background for overview, hide it.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call toggleBar >/dev/null # the bar is off by default
sleep 0.5
Y=776
click() { call simButton $1 $2 true 0; call simButton $1 $2 false 0; sleep 1.2; }
echo "--- toggle map"; click 1230 $Y; call dump | grep -E "^MAP"
/home/ivan/dev/krellis/tools/shot map >/dev/null
echo "--- hover the edge tab to bring the map back"
read -r gx gy gw gh < <(call dump | grep -E "^MAP" | sed -E 's/.*geometry ([0-9]+),([0-9]+) ([0-9]+)x([0-9]+).*/\1 \2 \3 \4/')
sleep 2.5; call dump | grep -E "^MAP" | sed -E 's/.*(open [0-9.]+ pinned [0-9]).*/collapsed: \1/'
call simMotion $((gx + gw - 8)) $((gy + gh - 30)); sleep 0.8; call dump | grep -E "^MAP" | sed -E 's/.*(open [0-9.]+ pinned [0-9]).*/hovered: \1/'
echo "--- tiles under the pointer"
for x in 1060 1100 1140 1180; do echo "x=$x '$(call barHit $x 600)'"; done
echo "--- click middle tile"; click 1140 600; call dump | grep -E "^camera|framed"
/home/ivan/dev/krellis/tools/shot map-framed >/dev/null
echo "--- click the framed tile again: zooms out"; click 1140 600; call dump | grep -E "^camera|framed"
echo "--- click map background (overview)"; click 1100 650; call dump | grep -E "^camera|framed"
echo "--- toggle map off"; click 1230 $Y; sleep 0.6; call dump | grep -E "^MAP" || echo "map destroyed"
pkill -P "$$" -x alacritty
