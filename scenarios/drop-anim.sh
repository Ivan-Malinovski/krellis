# glide=false with landMs>0: dropping a dragged window, and the neighbours reflowing while it hovers, must play
# the short glide instead of jumping. Samples every window's drawn x through a seam drop.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
snap() { call dump | grep -E "^p[0-9]+ " | sed -E "s/^p[0-9]+ 'win-(.)' screen ([-0-9.]+),.* drawn [^ ]+ drawnpos ([-0-9.]+),.*/\\1 s\\2 d\\3/" | tr '\n' ' '; echo; }
: >"$NEST_OUT/hover.txt"; : >"$NEST_OUT/preview.txt"; : >"$NEST_OUT/drop.txt"
call simDragStart 0 200 300
for x in 300 500 700 853; do call simDragMove $x 400; for i in 1 2 3 4 5 6; do snap >>"$NEST_OUT/preview.txt"; done; done
for i in $(seq 1 14); do snap >>"$NEST_OUT/hover.txt"; done
call simDragEnd true
for i in $(seq 1 25); do snap >>"$NEST_OUT/drop.txt"; done
# Count distinct drawn x values strictly between the start and end of each movement.
inter() { awk -v w="$1" '{ for (i=1;i<=NF;i++) if ($i==w) { x=substr($(i+2),2)+0; if (!(x in seen)) { seen[x]=1; n++; v[n]=x } } }
  END { lo=v[1]; hi=v[n]; for (k=1;k<=n;k++) { if (v[k]>lo && v[k]<hi || v[k]<lo && v[k]>hi) m++ } print m+0 }' "$2"; }
cat "$NEST_OUT/hover.txt" "$NEST_OUT/drop.txt" >"$NEST_OUT/after.txt"
b=$(inter B "$NEST_OUT/after.txt"); a=$(inter A "$NEST_OUT/drop.txt")
echo "neighbour B intermediate positions: $b, dropped A intermediate positions: $a"
rc=0; [ "$b" -ge 2 ] && [ "$a" -ge 2 ] && echo "ok neighbour reflow and drop both glide" || { echo "FAIL jump"; rc=1; }
pkill -P "$$" -x alacritty
exit $rc
