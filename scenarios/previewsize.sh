# While previewing a drop, the other windows really take their previewed size (not a stretched paint).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
echo "--- before"; call dump | grep -E "^p"
call simDragStart 0 200 400
for y in 300 500 700 775; do call simDragMove 640 $y; sleep 0.05; done
# Sample mid-animation and at rest: a window is never drawn with a different aspect than its frame.
stretched=0
for i in 1 2 3 4 5 6 7 8; do
  call dump | grep -E "^p[0-9] " | while read -r line; do
    echo "$line" | sed -E 's/.* frame [0-9.,]+ ([0-9.]+)x([0-9.]+) .* drawn ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/' | awk '{ if ($3>0 && $1>0) { a=$1/$2; b=$3/$4; d=(a>b?a/b:b/a); if (d>1.02) print "STRETCH", $0 } }'
  done >>"$NEST_OUT/stretch.txt"
  sleep 0.12
done
[ -s "$NEST_OUT/stretch.txt" ] && stretched=1
sleep 0.4
echo "--- previewing A under B"; call dump | grep -E "DRAG|SLOT|^p"
prev=$(call dump | grep -E "^p2" | grep -oE "frame [0-9.,]+ [0-9.]+x[0-9.]+" )
call simDragEnd false; sleep 1.2
echo "--- after cancel"; call dump | grep -E "^p"
after=$(call dump | grep -E "^p2" | grep -oE "frame [0-9.,]+ [0-9.]+x[0-9.]+")
rc=0
if [ $stretched = 1 ]; then echo "FAIL stretched drawing:"; head -3 "$NEST_OUT/stretch.txt"; rc=1; else echo "OK never drawn stretched"; fi
case "$prev" in *" 628.0x388.0") echo "OK preview really resized B ($prev)";; *) echo "FAIL preview did not resize B ($prev)"; rc=1;; esac
case "$after" in *" 415.0x788.0") echo "OK cancel restored B";; *) echo "FAIL cancel left B at $after"; rc=1;; esac
pkill -P "$$" -x alacritty
exit $rc
