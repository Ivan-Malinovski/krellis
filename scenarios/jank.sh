# Sample drawn position/size of every window across a layout change (closing the middle window).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & pids="$pids $!"; sleep 2; done
sleep 1
echo "--- before"; call dump | grep -E "^p"
mid=$(echo $pids | awk '{print $2}')
kill $mid
t0=$(date +%s%3N)
for i in $(seq 1 40); do
  now=$(date +%s%3N)
  call dump | grep -E "^p[0-9] " | sed -E "s/^(p[0-9]).* frame ([0-9.,]+) ([0-9.]+x[0-9.]+) .* drawn ([0-9.]+x[0-9.]+) drawnpos ([0-9.,]+).*/\1 frame \2 \3 drawn \5 \4/" | sed "s/^/$((now-t0)) /"
done
# Closing the middle window only changes widths: no sampled window may ever be drawn at another height or jump in width.
awk '$3=="frame" { split($5,d,"x"); if (d[2]+0 != 788) bad++; w=d[1]+0; if (pw && (w-pw>140 || pw-w>140)) bad++; pw=w } END { if (bad) { print "FAIL animation pops (" bad ")"; exit 1 } print "OK animation continuous" }' <(grep -E '^[0-9]+ p3 ' "$NEST_OUT/log.txt")
rc=$?
pkill -P "$$" -x alacritty
exit $rc
