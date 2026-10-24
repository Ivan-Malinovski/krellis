# Drag the middle of three columns out and drop it back in: nothing else should change size.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- start"; call dump | grep -E "^p|ORDER"
echo "--- lift B and return to each of its original seams"
for seam in 427 853; do
  call simDragStart 1 640 400
  call simDragMove "$seam" 400
  sleep 0.4; call dump | grep -E "DRAG|SLOT|^p"
  call simDragEnd true; sleep 1.2; call dump | grep -E "^p|ORDER"
done
widths=$(call dump | grep -oE "^p[0-9] .* frame [0-9.,]+ [0-9]+" | awk '{print $NF}' | sort -u | tr '\n' ' ')
echo "widths after drop: $widths"
case "$widths" in "415 ") echo "OK all columns unchanged"; rc=0;; *) echo "FAIL columns resized"; rc=1;; esac
pkill -P "$$" -x alacritty
exit $rc
