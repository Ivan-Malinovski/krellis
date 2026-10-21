# Five windows exceed the pixel minimums; then close one and sample the layout tween.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
for n in A B C D E; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  if [ "$n" = C ]; then closing_client=$!; fi
  sleep 1.5
done
sleep 1.5
echo "--- five"; call dump
/home/ivan/dev/kwin-trellis/tools/shot five >/dev/null
kill "$closing_client"
sleep 0.08; echo "--- after close (mid)"; call dump
/home/ivan/dev/kwin-trellis/tools/shot closing >/dev/null
sleep 1.5; echo "--- after close (rest)"; call dump
/home/ivan/dev/kwin-trellis/tools/shot four >/dev/null
pkill -P "$$" -x alacritty
