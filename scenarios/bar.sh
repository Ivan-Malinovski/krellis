# The bottom bar: hit regions, saving a framing, going to it, history buttons, removing it.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
call toggleBar >/dev/null # the bar is off by default
sleep 0.5
Y=776
scan() { prev=""; for x in $(seq 0 6 1279); do k=$(call barHit $x $Y); if [ "$k" != "$prev" ]; then echo "x=$x '$k'"; prev=$k; fi; done; }
echo "--- scan"; scan
/home/ivan/dev/kwin-trellis/tools/shot bar >/dev/null
echo "--- focus B (index 1), save framing, overview, go to framing"
call focusIndex 1; sleep 1.2
click() { call simButton $1 $Y true 0; call simButton $1 $Y false 0; sleep 1.2; }
save=$(scan | grep "'save'" | head -1 | sed 's/x=\([0-9]*\).*/\1/'); echo "save at $save"
click $((save+4))
call simText "Reading"; call simKey 16777220 true; call simKey 16777220 false; sleep 0.5
call dump | grep -E "^camera|framed"
ov=$(scan | grep "'overview'" | head -1 | sed 's/x=\([0-9]*\).*/\1/'); click $((ov+4)); echo "after overview"; call dump | grep -E "^camera|framed"
fr=$(scan | grep "'framing:" | head -1 | sed 's/x=\([0-9]*\).*/\1/'); echo "framing at $fr"; click $((fr+4)); echo "after framing"; call dump | grep -E "^camera|framed"
bk=$(scan | grep "'back'" | head -1 | sed 's/x=\([0-9]*\).*/\1/'); click $((bk+4)); echo "after back"; call dump | grep -E "^camera|framed"
/home/ivan/dev/kwin-trellis/tools/shot bar2 >/dev/null
rm=$(scan | grep "'rm:" | head -1 | sed 's/x=\([0-9]*\).*/\1/'); click $((rm+4)); echo "after remove"; scan
pkill -P "$$" -x alacritty
