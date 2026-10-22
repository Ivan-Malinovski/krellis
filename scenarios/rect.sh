# Rectangle-select gesture (Meta+drag): rubber band, "Release to focus" candidate, release, Esc cancel.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
CAS=$((0x10000000))   # Meta
for n in A B C; do
  alacritty --title "win-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 2
done
sleep 1
echo "--- band around B (x 440..850)"
call simButton 440 120 true $CAS; call simMotion 600 300; call simMotion 850 700; sleep 0.6; call dump | grep -E "^RECT|^camera"
/home/ivan/dev/krellis/tools/shot rect-band >/dev/null
echo "--- release"; call simButton 850 700 false 0; sleep 1.4; call dump | grep -E "^RECT|^camera|framed"
/home/ivan/dev/krellis/tools/shot rect-focused >/dev/null
echo "--- overview, band across B and C, then Esc"
call overview; sleep 1.2
call simButton 100 100 true $CAS; call simMotion 900 500; sleep 0.5; call dump | grep -E "^RECT|^camera"
call simKey 16777216 true; sleep 1.2; call dump | grep -E "^RECT|^camera|framed"
echo "--- tiny band falls back to the current framing"
call simButton 300 300 true $CAS; call simMotion 303 303; call dump | grep -E "^RECT"
call simButton 303 303 false 0; sleep 1; call dump | grep -E "^camera|framed"
pkill -P "$$" -x alacritty
