# Dwindle judges a tile by its size on screen. Three columns of 427x800 are tall in the layout, but zoomed onto the last
# one it fills the screen (1280x800), so a new window must open beside it (full height), not under it (half height).
# Seeded with spawn-dwindle-full.rc (spawnTarget=end); the scenario switches to dwindle once the columns exist.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
order() { call dump | awk '/^ORDER/ { $1=""; print; exit }' | xargs; }
height() { call dump | sed -nE "s/^$1 .* screen [-0-9.]+,[-0-9.]+ [0-9.]+x([0-9.]+).*/\1/p" | head -1; }
width() { call dump | sed -nE "s/^$1 .* screen [-0-9.]+,[-0-9.]+ ([0-9.]+)x[0-9.]+.*/\1/p" | head -1; }
rc=0
check() { [ "$2" = "$3" ] && echo "ok $1: $2" || { echo "FAIL $1: got '$2' want '$3'"; rc=1; }; }
open() { alacritty --title "$1" -e sleep 600 >/dev/null 2>&1 & sleep 2.5; }
sleep 1
open w1; open w2; open w3
check three-columns "$(order)" "p1 p2 p3"
call toggleMaximize 2; sleep 1.5
check framed-p3 "$(call dump | grep -E '^framed')" "framed 'p3'"
echo "p3 on screen: $(width p3)x$(height p3)"
sed -i 's/spawnTarget=end/spawnTarget=dwindle/' "$XDG_CONFIG_HOME/krellisrc"
call reloadSettings; sleep 0.5
open w4
check order "$(order)" "p1 p2 p3 p4"
# Side by side: p4 is as tall as p1 (both scaled by the same camera). Underneath it would be about half.
ratio=$(awk -v a="$(height p4)" -v b="$(height p1)" 'BEGIN { printf "%.1f", a / b }')
check p4-beside-p3 "$ratio" "1.0"
pkill -x alacritty
exit $rc
