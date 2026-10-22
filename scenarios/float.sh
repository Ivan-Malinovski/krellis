# Floating (overlay) windows: pop out, glide, reposition by drag, dock by drag, pop back in.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- float B"; call toggleFloat 1
sleep 0.15; call dump | grep -E "p[0-9]"
sleep 1.0; echo "(settled)"; call dump
/home/ivan/dev/krellis/tools/shot float >/dev/null
echo "--- reposition B (float) by dragging within the canvas centre"
call simDragStart 1 640 400
for x in 600 500 400; do call simDragMove $x 300; sleep 0.05; done
sleep 0.4; call dump | grep -E "DRAG|SLOT|p2"
call simDragEnd true; sleep 1.2; call dump | grep -E "p[0-9]|ORDER"
echo "--- dock B by dragging to the right frame band"
call simDragStart 1 400 300
for x in 800 1100 1250 1277; do call simDragMove $x 400; sleep 0.05; done
sleep 0.5; call dump | grep -E "DRAG|SLOT"
call simDragEnd true; sleep 1.2; call dump | grep -E "p[0-9]|ORDER"
echo "--- float C, then pop it back in"
call toggleFloat 2; sleep 1.0; call dump | grep -E "p3|ORDER"
call toggleFloat 2; sleep 1.0; call dump | grep -E "p[0-9]|ORDER"
pkill -P "$$" -x alacritty
