# Drag-and-drop docking through the synthetic drag slots: seam drop, frame-band drop, cancel.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- start"; call dump
echo "--- press on A, small move (below threshold)"
call simDragStart 0 200 300; call simDragMove 203 301; call dump | grep -E "DRAG|ORDER"
echo "--- drag A to the B|C seam (x=853 before the source collapses)"
for x in 300 500 700 853; do call simDragMove $x 400; sleep 0.05; done
sleep 0.3; call dump | grep -E "DRAG|SLOT|ORDER"
sleep 0.5; echo "(settled)"; call dump; ls /tmp/krellis-nest/out/
$KRELLIS_ROOT/tools/shot seam-preview >/dev/null
echo "--- release"; call simDragEnd true
sleep 0.15; call dump | grep -E "p[0-9]|ORDER"
sleep 1.0; echo "(settled)"; call dump
$KRELLIS_ROOT/tools/shot after-seam >/dev/null
echo "--- drag C (index 2) to the left frame band"
call simDragStart 2 1000 400
for x in 800 400 100 40 6 3; do call simDragMove $x 400; sleep 0.05; done
sleep 0.6; call dump | grep -E "DRAG|SLOT"
call simDragEnd true; sleep 1.2; call dump
echo "--- cancel"
call simDragStart 1 700 400
for x in 600 300 100; do call simDragMove $x 300; sleep 0.05; done
sleep 0.5; call dump | grep -E "DRAG|SLOT"
call simDragEnd false; sleep 1.2; call dump
pkill -P "$$" -x alacritty
