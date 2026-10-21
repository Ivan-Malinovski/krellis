# Minimap: drag a square over tiles to frame the ones it touches; Esc cancels; clicking the mode pill toggles it.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
for n in A B C D; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
call toggleMap >/dev/null; sleep 1.2
call overview; sleep 1
read -r gx gy gw gh < <(call dump | grep -E "^MAP" | sed -E 's/.*geometry ([0-9]+),([0-9]+) ([0-9]+)x([0-9]+).*/\1 \2 \3 \4/')
cardL=$((gx + gw - 256)); cardT=$((gy + gh - (gh - 338)))
sx=$((cardL + 12)); sy=$((cardT + 30)); tw=58
reveal() { call simMotion $((gx + gw - 8)) $((gy + gh - 30)); sleep 0.8; }
reveal
cam() { call dump | grep -E "^camera"; }
echo "stage at $sx,$sy"
echo "--- drag over tiles A..C"
call simButton $((sx + 10)) $((sy + 40)) true 0
for x in 30 60 100 140; do call simMotion $((sx + x)) $((sy + 60)); done
sleep 0.6; call dump | grep -E "^MAP"
/home/ivan/dev/kwin-trellis/tools/shot map-select >/dev/null
call simButton $((sx + 145)) $((sy + 60)) false 0; sleep 1.2; cam
echo "--- drag over tiles C..D"
call overview; sleep 1
call simButton $((sx + 120)) $((sy + 40)) true 0
for x in 150 200 225; do call simMotion $((sx + x)) $((sy + 60)); done
call simButton $((sx + 225)) $((sy + 60)) false 0; sleep 1.2; cam
echo "--- drag then Esc cancels"
call overview; sleep 1
call simButton $((sx + 10)) $((sy + 40)) true 0
call simMotion $((sx + 100)) $((sy + 60)); sleep 0.3
call simKey 16777216 true; call simKey 16777216 false; sleep 0.3
call simButton $((sx + 100)) $((sy + 60)) false 0; sleep 1; cam
echo "--- click without moving still frames one tile"
call simButton $((sx + 90)) $((sy + 40)) true 0; call simButton $((sx + 90)) $((sy + 40)) false 0; sleep 1.2; cam
echo "--- mode pill"
call dump | grep -E "^MAP"
px=$((cardL + 179)); py=$((cardT + 15)); /home/ivan/dev/kwin-trellis/tools/shot map-pill >/dev/null
call simButton $px $py true 0; call simButton $px $py false 0; sleep 0.8; call dump | grep -E "^MAP"
call simButton $px $py true 0; call simButton $px $py false 0; sleep 0.8; call dump | grep -E "^MAP"
pkill -P "$$" -x alacritty
