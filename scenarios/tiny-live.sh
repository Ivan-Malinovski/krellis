# iconBelow=0 (seed scenarios/tiny-live.rc): twelve windows on a 1280 px output are ~100 px wide, far under the old
# icon cutoff. They must stay live (no ICON), be asked for their minimum at the tile's own proportions, and be drawn
# with that same aspect (nothing stretched or letterboxed). Live is now the default; EXPECT=icons with scenarios/icons.rc
# (iconBelow=160) checks that the opt-in cutoff still shows icons.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
for n in $(seq 1 12); do alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 & sleep 0.9; done
sleep 2
call dump | grep -E "^p[0-9]+ " | cut -c1-260
icons=$(call dump | grep -cE "^p[0-9]+ .* ICON" || true)
bad=$(call dump | grep -E "^p[0-9]+ " | sed -E 's/.* screen [0-9.,]+ ([0-9.]+)x([0-9.]+) frame [0-9.,]+ ([0-9.]+)x([0-9.]+) .*/\1 \2 \3 \4/' \
  | awk '{ a=$1/$2; b=$3/$4; d=(a>b?a/b:b/a); if (d>1.02) n++ } END { print n+0 }')
if [ "${EXPECT:-live}" = live ]; then
  [ "$icons" = 0 ] && echo "PASS no icon tiles" || echo "FAIL $icons icon tiles with iconBelow=0"
  [ "$bad" = 0 ] && echo "PASS frames keep the tile's proportions" || echo "FAIL $bad frames differ in aspect from their tile"
else
  [ "$icons" -gt 0 ] && echo "PASS opt-in cutoff shows icons ($icons)" || echo "FAIL opt-in cutoff showed no icons"
fi
pkill -P "$$" -x alacritty
