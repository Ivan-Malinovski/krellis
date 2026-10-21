# Global-shortcut commands (next/previous panel, maximize, float, hide, close on the active window), driven over D-Bus.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- start"; call dump | head -3
echo "--- next"; call nextPanel; sleep 1.2; call dump | head -3
echo "--- next"; call nextPanel; sleep 1.2; call dump | head -3
echo "--- previous"; call previousPanel; sleep 1.2; call dump | head -3
echo "--- maximize active"; call toggleMaximizeActive; sleep 1.2; call dump | head -3
/home/ivan/dev/kwin-trellis/tools/shot keys-max >/dev/null
call toggleMaximizeActive; sleep 1
echo "--- float active"; call toggleFloatActive; sleep 1.2; call dump | head -5
call toggleFloatActive; sleep 1
echo "--- hide active"; call toggleHideActive; sleep 1.2; call dump | head -5
echo "--- close active"; call closeActive; sleep 1; call dump | head -5
pkill -P "$$" -x alacritty
