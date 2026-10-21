# Drag the left of three columns under the middle one and back out, repeatedly: the others must not shrink.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- start"; call dump | grep -E "^p|ORDER"
rc=0
: "${WHO:=A}" "${UNDER_X:=640}" "${BACK_XS:=500 300 100 10}"
widths() { call dump | grep -oE "^p[0-9] .* frame [0-9.,]+ [0-9.]+x" | sed -E "s/^(p[0-9]).* ([0-9.]+)x$/\1=\2/" | tr '\n' ' '; }
# index and centre of 'win-A' in the current order
where() { call dump | awk -v t="'win-$1'" '/^p[0-9]/ { if ($2==t) { split($6,a,","); split($7,b,"x"); printf "%d %d %d\n", n, 200, 400 } n++ }'; }
for cycle in 1 2 3 4; do
  read idx cx cy < <(where $WHO)
  echo "--- cycle $cycle: lift $WHO (index $idx at $cx,$cy), dock under the middle window"
  call simDragStart "$idx" "$cx" "$cy"
  for y in 300 500 700 775; do call simDragMove $UNDER_X $y; sleep 0.05; done
  sleep 0.4; call dump | grep -E "DRAG|SLOT"
  call simDragEnd true; sleep 1.2; echo "widths: $(widths)"; call dump | grep -E "^p"
  read idx cx cy < <(where $WHO)
  echo "--- cycle $cycle: lift $WHO again (index $idx at $cx,$cy), drop back at the left edge"
  call simDragStart "$idx" "$cx" "$cy"
  for x in $BACK_XS; do call simDragMove $x 400; sleep 0.05; done
  sleep 0.4; call dump | grep -E "DRAG|SLOT"
  call simDragEnd true; sleep 1.2; echo "widths: $(widths)"; call dump | grep -E "^p"
done
pkill -P "$$" -x alacritty
exit $rc
