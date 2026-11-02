# glide=false with landMs>0: dropping a dragged window, and the neighbours reflowing while it hovers, must play
# the short glide instead of jumping. Samples every window's drawn x through a seam drop.
#
# The hover is not instantaneous: DragSession settles a new drop target after 150 ms (SettleMs) and the
# neighbours then land over landMs (200 ms), so each simDragMove is followed by samples taken across both.
# Sampling straight after the move only ever sees "before the settle" and "after the landing" - a jump.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
snap() { call dump | grep -E "^p[0-9]+ " | sed -E "s/^p[0-9]+ 'win-(.)' screen ([-0-9.]+),.* drawn [^ ]+ drawnpos ([-0-9.]+),.*/\\1 s\\2 d\\3/" | tr '\n' ' '; echo; }
: >"$NEST_OUT/hover.txt"; : >"$NEST_OUT/drop.txt"
call simDragStart 0 200 300
for x in 300 500 700 853; do
  call simDragMove $x 400
  for i in $(seq 1 12); do snap >>"$NEST_OUT/hover.txt"; sleep 0.05; done
done
call simDragEnd true
for i in $(seq 1 25); do snap >>"$NEST_OUT/drop.txt"; sleep 0.02; done
# Distinct drawn x values for one window, and how far it travelled: a jump shows 2 values over the whole span,
# a glide shows the positions in between.
spread() { awk -v w="$1" '{ for (i=1;i<=NF;i++) if ($i==w) { x=substr($(i+2),2)+0; if (!(x in seen)) { seen[x]=1; n++; if (n==1) { lo=x; hi=x }; if (x<lo) lo=x; if (x>hi) hi=x } } }
  END { printf "%d %d\n", n+0, (n>1 ? hi-lo : 0) }' "$2"; }
nb=0; nspan=0
for w in B C; do
  read -r count span <<<"$(spread "$w" "$NEST_OUT/hover.txt")"
  echo "neighbour $w: $count distinct drawn x over $span px of reflow"
  if [ "$span" -gt "$nspan" ]; then nb=$count; nspan=$span; fi
done
read -r acount aspan <<<"$(spread A "$NEST_OUT/drop.txt")"
echo "dropped A: $acount distinct drawn x over $aspan px onto its slot"
rc=0
# A neighbour that really reflowed (>= 100 px) must have been drawn at least twice in between.
if [ "$nspan" -ge 100 ] && [ "$nb" -ge 4 ]; then echo "ok neighbour reflow glides"; else echo "FAIL neighbour reflow jumps ($nb drawn x over $nspan px)"; rc=1; fi
if [ "$aspan" -ge 100 ] && [ "$acount" -ge 4 ]; then echo "ok drop glides"; else echo "FAIL drop jumps ($acount drawn x over $aspan px)"; rc=1; fi
pkill -P "$$" -x alacritty
exit $rc
