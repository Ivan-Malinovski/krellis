# Three terminals tile into a row; focus the middle one, then overview.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis "$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1.5
echo "--- initial"; call org.kde.kwin.KWin.TrellisEffect.dump 2>&1 || qdbus6 org.kde.KWin /Trellis
/home/ivan/dev/kwin-trellis/tools/shot 1-initial
call org.kde.kwin.KWin.TrellisEffect.focusIndex 1; sleep 1.2
echo "--- focus B"; call org.kde.kwin.KWin.TrellisEffect.dump
/home/ivan/dev/kwin-trellis/tools/shot 2-focus-b
call org.kde.kwin.KWin.TrellisEffect.overview; sleep 1.2
echo "--- overview"; call org.kde.kwin.KWin.TrellisEffect.dump
/home/ivan/dev/kwin-trellis/tools/shot 3-overview
cp "$NEST_OUT"/*.png /tmp/ 2>/dev/null
pkill -P "$$" -x alacritty
