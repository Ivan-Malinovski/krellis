# Alt+scroll zooms one column and row per scroll gesture; a flick is one step; a title bar scrolls with no key;
# Ctrl+scroll is left alone; the minimap names the mode and its (?) opens the shortcut sheet.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
ALT=$((0x08000000)); CTRL=$((0x04000000))
for n in A B C D; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
cam() { call dump | grep -E "^camera"; }
echo "--- overview"; call overview; sleep 1.2; cam
echo "--- Ctrl+scroll in: ignored"; call simAxis -15 100 400 $CTRL; sleep 1; cam
echo "--- Alt+scroll in, a flick of 6 events"
for i in 1 2 3 4 5 6; do call simAxis -15 100 400 $ALT; sleep 0.04; done
sleep 1.2; cam
echo "--- after a pause, another step in"; sleep 2; call simAxis -15 100 400 $ALT; sleep 0.3; cam; sleep 2; cam; call simAxis -15 100 400 $ALT; sleep 2; cam
echo "--- Alt+scroll out"; call simAxis 15 100 400 $ALT; sleep 1.2; cam
echo "--- Meta+scroll out"; call simAxis 15 100 400 $((0x10000000)); sleep 1.2; cam
call dump | grep -E "^p"
echo "--- title bar scroll, no key"
call overview; sleep 1.2
call simAxis -15 160 20 0; sleep 1.2; cam
call simAxis 15 160 20 0; sleep 1.2; cam
echo "--- scroll over the window body with no key: not a zoom"
call simAxis -15 160 400 0; sleep 1; cam
echo "--- minimap mode label and help sheet"
call toggleBar >/dev/null; sleep 0.5
click() { call simButton $1 $2 true 0; call simButton $1 $2 false 0; sleep 1.2; }
click 1230 776; call dump | grep -E "^MAP"
read -r gx gy gw gh < <(call dump | grep -E "^MAP" | sed -E 's/.*geometry ([0-9]+),([0-9]+) ([0-9]+)x([0-9]+).*/\1 \2 \3 \4/')
hx=$((gx + gw - 20)); hy=$((gy + gh - 186 + 15))
echo "help button at $hx,$hy"
call simMotion $((gx + gw - 8)) $((gy + gh - 30)); sleep 0.8
call simMotion $hx $hy; sleep 0.8; call dump | grep -E "^MAP"
/home/ivan/dev/krellis/tools/shot map-help-early >/dev/null; sleep 2
/home/ivan/dev/krellis/tools/shot map-help >/dev/null
call simMotion 600 300; sleep 0.8; call dump | grep -E "^MAP"
call toggleFloatFirst >/dev/null 2>&1 || call setFloatFirst true >/dev/null 2>&1; sleep 0.8; call dump | grep -E "^MAP"
/home/ivan/dev/krellis/tools/shot map-floating >/dev/null
pkill -P "$$" -x alacritty
