# While previewing a drop, the other windows really take their previewed size (not a stretched paint).
# Needs a glide=false rc with landMs=0 (or glide=true): the short landing glide scales a window for ~200 ms by design.
#
# The exact previewed rectangles depend on the tile order and gaps, so this measures the invariant instead of
# pinning pixels: the window that gives the drop slot its room is really moved and re-sized by the compositor
# (its frame loses about half of one axis: that is the reflow, not a stretched paint), and cancelling puts every
# frame back exactly where it was. Every sample, mid-animation too, must draw each window at its frame's aspect.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in A B C; do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 2; done
sleep 1
# panel id and frame size per window, e.g. "2 414.7 788.0"
frames() { call dump | grep -E "^p[0-9] " | sed -E "s/^p([0-9]+) .* frame [0-9.,]+ ([0-9.]+)x([0-9.]+) .*/\1 \2 \3/"; }
echo "--- before"; call dump | grep -E "^p"
frames >"$NEST_OUT/before.frames"
call simDragStart 0 200 400
# One move onto the bottom band of the middle tile, then leave the drop target to settle: retargeting while the
# tiles reflow under the pointer walks the slot from half a tile to a whole one, and only the settled preview is
# worth measuring. The samples below span the 150 ms settle and the 200 ms landing.
call simDragMove 640 775
# Sample mid-animation and at rest: a window is never drawn with a different aspect than its frame.
stretched=0
: >"$NEST_OUT/stretch.txt"
for i in 1 2 3 4 5 6 7 8; do
  call dump | grep -E "^p[0-9] " | while read -r line; do
    echo "$line" | sed -E 's/.* frame [0-9.,]+ ([0-9.]+)x([0-9.]+) .* drawn ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/' | awk '{ if ($3>0 && $1>0) { a=$1/$2; b=$3/$4; d=(a>b?a/b:b/a); if (d>1.02) print "STRETCH", $0 } }'
  done >>"$NEST_OUT/stretch.txt"
  sleep 0.12
done
[ -s "$NEST_OUT/stretch.txt" ] && stretched=1
sleep 0.4
echo "--- previewing"; call dump | grep -E "DRAG|SLOT|^p"
frames >"$NEST_OUT/preview.frames"
slots=$(call dump | grep -c '^SLOT ')
call simDragEnd false; sleep 1.2
echo "--- after cancel"; call dump | grep -E "^p"
frames >"$NEST_OUT/after.frames"
rc=0
if [ $stretched = 1 ]; then echo "FAIL stretched drawing:"; head -3 "$NEST_OUT/stretch.txt"; rc=1; else echo "PASS never drawn stretched"; fi
# The lifted window is p1; every other frame that changed during the preview did so for real.
reflowed=$(awk 'NR==FNR { w[$1]=$2+0; h[$1]=$3+0; next }
  $1 != 1 && ($2 != w[$1] || $3 != h[$1]) { printf "%s %sx%s (was %sx%s)\n", $1, $2, $3, w[$1], h[$1] }' "$NEST_OUT/before.frames" "$NEST_OUT/preview.frames")
echo "reflowed for the slot: ${reflowed:-none}"
half=$(awk 'NR==FNR { w[$1]=$2+0; h[$1]=$3+0; next }
  $1 != 1 { x=$2/w[$1]; y=$3/h[$1]; if ((x>0.4 && x<0.6) || (y>0.4 && y<0.6)) print $1 }' "$NEST_OUT/before.frames" "$NEST_OUT/preview.frames")
if [ -n "$reflowed" ] && [ "$slots" -ge 1 ] && [ -n "$half" ]; then
  echo "PASS preview really resized the neighbour that gave up the slot (panel $half took about half of one axis)"
else
  echo "FAIL preview did not re-size a neighbour (slot rows $slots, reflowed '${reflowed:-none}', halved '${half:-none}')"; rc=1
fi
# Cancel puts every frame back exactly: the preview was a preview.
left=$(awk 'NR==FNR { w[$1]=$2; h[$1]=$3; next }
  ($2 != w[$1] || $3 != h[$1]) { printf "%s %sx%s (was %sx%s) ", $1, $2, $3, w[$1], h[$1] }' "$NEST_OUT/before.frames" "$NEST_OUT/after.frames")
if [ -z "$left" ]; then echo "PASS cancel restored every frame"; else echo "FAIL cancel left ${left}"; rc=1; fi
pkill -P "$$" -x alacritty
exit $rc
