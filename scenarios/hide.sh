# Hide (minimize) the middle window, then restore it; the row should re-share and return.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- three"; call dump | tail -3
call toggleHide 1; sleep 1.5
echo "--- B hidden"; call dump | tail -3
/home/ivan/dev/krellis/tools/shot hidden >/dev/null
call toggleHide 1; sleep 1.5
echo "--- B restored"; call dump | tail -3
pkill -P "$$" -x alacritty
