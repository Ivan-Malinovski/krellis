# Drive the input handlers through synthetic D-Bus events: pinch, pan drag, wheel step, dock click.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
CA=$((0x04000000 | 0x08000000))   # Ctrl | Alt
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- pinch in at centre"; call simPinch 1.4 640 400; call dump | head -2
/home/ivan/dev/krellis/tools/shot pinch >/dev/null
call simPinchEnd; sleep 1.2; echo "--- after release"; call dump | head -2
echo "--- overview"; call overview; sleep 1.2
echo "--- Ctrl+Alt wheel step at B"; call simAxis -15 640 400 $CA; sleep 1.2; call dump | head -2
echo "--- overview, pan drag"; call overview; sleep 1.2
call simButton 600 400 true $CA; call simMotion 500 400; call simButton 500 400 false 0; sleep 1.2; call dump | head -2
echo "--- hide B, click chip"; call toggleHide 1; sleep 1.2
call dump | head -0
call simButton 640 770 true 0
sleep 1.2; call dump | tail -3
pkill -P "$$" -x alacritty
