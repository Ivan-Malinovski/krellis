# Zoomed in, the neighbours' edges stay in view: dimmed, inert, and a click widens the frame to take one in.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
echo "--- overview"; call dump | grep -E "^camera|^framed|^p"
call toggleMaximize 1; sleep 1.5
echo "--- B framed"; call dump | grep -E "^camera|^framed|^p"
/home/ivan/dev/krellis/tools/shot peek-framed >/dev/null
rc=0
# p2 is the last column: a gutter on its left only, 40 px of the neighbour's edge on a 1280 px screen.
set -- $(call dump | grep -E "^p2 " | sed -E 's/.* screen ([-0-9.]+),([0-9.]+) .*/\1 \2/')
left=${1%.*}
if [ "$left" -eq 46 ]; then echo "OK gutter of 40 px (+6 gap) on the left: framed tile at $left"; else echo "FAIL framed tile at $left, expected 46"; rc=1; fi
echo "--- press on the neighbour's edge: inert until released"
call simButton 15 400 true 0; sleep 0.3
call dump | grep -E "^framed"
call simButton 15 400 false 0; sleep 1.5
fr=$(call dump | grep -E "^framed")
echo "--- after the click"; call dump | grep -E "^camera|^framed|^p"
case "$fr" in "framed 'p2'") echo "FAIL the click did not widen the frame"; rc=1;; *) echo "OK frame widened: $fr";; esac
/home/ivan/dev/krellis/tools/shot peek-widened >/dev/null
echo "--- zoom back into the first column: right gutter only"
call toggleMaximize 0; sleep 1.5; call dump | grep -E "^framed|^p1 "
right=$(call dump | grep -E "^p3 " | sed -E 's/.* screen ([-0-9.]+),.*/\1/'); right=${right%.*}
if [ "$right" -gt 1234 ] && [ "$right" -lt 1280 ]; then echo "OK the next column starts at $right, inside the screen"; else echo "FAIL next column at $right"; rc=1; fi
pkill -P "$$" -x alacritty
exit $rc
