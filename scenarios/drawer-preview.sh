#!/usr/bin/env bash
# Squeezing an edge tile under the collapse width previews the drawer it will become (a strip of the edge and
# a label); backing off the threshold removes it; and clicking a closed drawer's tile on the minimap slides
# that drawer out and keeps it out while the pointer is away.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
for n in A B C D; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call overview; sleep 0.8
call simNativeResizeStart 0 1 0.5 | grep -qx true || echo "FAIL no native resize"
call simNativeResizeMove -100 0 >/dev/null; sleep 0.8
check "no preview while the tile is still wide" "$(call dump | grep -c '^SQUEEZE')" 0
call simNativeResizeMove -900 0 >/dev/null; sleep 0.8
call dump | grep '^SQUEEZE'
check "preview once squeezed under the threshold" "$(call dump | grep -c '^SQUEEZE')" 1
check "it is on the left edge" "$(call dump | grep -c '^SQUEEZE edge 0 ')" 1
$KRELLIS_ROOT/tools/shot squeeze-preview >/dev/null
call simNativeResizeMove -100 0 >/dev/null; sleep 1.2
check "preview goes when the tile is widened again" "$(call dump | grep -c '^SQUEEZE')" 0
call simNativeResizeMove -900 0 >/dev/null; sleep 0.5
call simNativeResizeEnd true >/dev/null; sleep 1.5
check "release makes the drawer" "$(call dump | grep -c '^DRAWER')" 1
check "and the preview is gone" "$(call dump | grep -c '^SQUEEZE')" 0
echo "--- the drawer's tile on the minimap opens it"
call toggleBar >/dev/null; sleep 0.5
call simButton 1230 776 true 0 >/dev/null; call simButton 1230 776 false 0 >/dev/null; sleep 1.2
read -r gx gy gw gh < <(call dump | grep -E "^MAP" | sed -E 's/.*geometry ([0-9]+),([0-9]+) ([0-9]+)x([0-9]+).*/\1 \2 \3 \4/')
sleep 2.5
call simMotion $((gx + gw - 8)) $((gy + gh - 30)) >/dev/null; sleep 1.0
call dump | grep -E "^MAPTILE|^DRAWER"
read -r tx ty tw th < <(call dump | grep '^MAPTILE' | sed -E 's/.* at ([0-9.]+),([0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/')
cx=$(awk -v a="$tx" -v b="$tw" 'BEGIN{printf "%d", a + b / 2}'); cy=$(awk -v a="$ty" -v b="$th" 'BEGIN{printf "%d", a + b / 2}')
check "closed first" "$(call dump | grep '^DRAWER' | grep -c ' closed ')" 1
call simMotion $cx $cy >/dev/null; call simButton $cx $cy true 0 >/dev/null; call simButton $cx $cy false 0 >/dev/null; sleep 1.2
call dump | grep -E "^DRAWER"
check "clicking it opens the drawer" "$(call dump | grep '^DRAWER' | grep -c ' open ')" 1
call simMotion $((cx - 5)) $((cy - 3)) >/dev/null; sleep 1.0
check "it stays open with the pointer on the map" "$(call dump | grep '^DRAWER' | grep -c ' open ')" 1
$KRELLIS_ROOT/tools/shot map-opened-drawer >/dev/null
call simMotion 5 400 >/dev/null; sleep 0.8
call simMotion 700 300 >/dev/null; sleep 1.2
check "after visiting it and leaving, it closes" "$(call dump | grep '^DRAWER' | grep -c ' closed ')" 1
pkill -P "$$" -x alacritty
