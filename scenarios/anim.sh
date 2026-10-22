# Sample the focus animation: dump screen-vs-frame rects while the camera spring runs.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call focusIndex 1
for i in 1 2 3 4 5 6; do
  /home/ivan/dev/krellis/tools/shot mid-$i >/dev/null
  echo "--- sample $i"; call dump | head -3
done
pkill -P "$$" -x alacritty
