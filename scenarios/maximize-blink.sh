# Maximize (a preset-placement layout glide): clients are asked for the size they are drawn at, step by step,
# never the final size ahead of the animation (the content "blink"). Samples frame vs drawn width.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
call dump | grep -E "^p"
t0=$(date +%s%N)
call toggleMaximize 1
rm -f "$NEST_OUT/samples.txt"
for i in $(seq 1 40); do
  now=$(( ($(date +%s%N) - t0) / 1000000 ))
  call dump | grep -E "^p2 " | sed -E "s/^/$now ms /" | sed -E 's/app .*drawn/drawn/' | tee -a "$NEST_OUT/samples.txt"
done
sleep 1
call dump | grep -E "^p"
rc=0
awk '{ for (i=1;i<=NF;i++) { if ($i=="screen") { split($(i+2),a,"x"); sw=a[1] } if ($i=="frame") { split($(i+2),b,"x"); fw=b[1] } } if (sw>0 && fw/sw > 1.2) { print "FAIL early final size:", $0; bad=1 } } END { exit bad }' "$NEST_OUT/samples.txt" && echo "ok contents follow the maximize glide" || rc=1
pkill -P "$$" -x alacritty
exit $rc
