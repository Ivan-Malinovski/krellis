# Sample appear / hide / restore flights via dump (FLY marks a window in flight).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- appear C"
alacritty --title win-C -e sh -c "echo C; sleep 600" >/dev/null 2>&1 &
for i in 1 2 3 4 5 6; do sleep 0.12; echo "t$i"; call dump | grep FLY; done
sleep 1
echo "--- hide B"
call toggleHide 1
for i in 1 2 3 4; do echo "t$i"; call dump | grep FLY; sleep 0.08; done
sleep 1
echo "--- restore B"
call toggleHide 1
for i in 1 2 3 4 5; do echo "t$i"; call dump | grep FLY; sleep 0.08; done
pkill -P "$$" -x alacritty
