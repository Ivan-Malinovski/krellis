#!/usr/bin/env bash
# The lone-window share is a reading width, so on a display used on its side it takes rows instead of columns:
# narrowing the width of a 800x1280 output would leave a tall sliver with gutters down both sides. NEST_SIZE in
# scenarios/lone-width-portrait.env turns the nested output.
#
# Sizes are compared as shares of the same tile measured with the share off, because a tile is painted inside
# its seam (~12 px on an 800 px output).
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
near() { awk -v a="$1" -v b="$2" -v tol="$3" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= tol) }' && echo yes || echo no; }
rect() { call dump | grep "^$1 '" | sed -nE 's/.*screen (-?[0-9.]+),(-?[0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/p'; }
geom() { rect "$1" | awk -v k="$2" '{print $k}'; }  # 1 x, 2 y, 3 w, 4 h
cam() { call dump | sed -nE "s/^camera .*\\(target [^ ]+ [^ ]+ ([0-9.]+) ([0-9.]+).*/\\$1/p"; }
lone() { call dump | grep -c "^$1 '.* LONE"; }
share() { awk -v a="$1" -v b="$2" 'BEGIN { printf "%.4f", a / b }'; }
config() {
    local wide=$1; shift
    { printf '[General]\nglide=true\nmap=false\nfollowTheme=false\ntoasts=false\nwhichKey=false\nloneWidth=%s\n' "$wide"
      if [ $# -gt 0 ]; then printf '[ViewRules]\n'; printf '%s\n' "$@"; fi
    } >"$XDG_CONFIG_HOME/krellisrc"
    call reloadSettings; sleep 1.5
}
trap 'kill ${a:-} ${b:-} 2>/dev/null || true' EXIT
c++ -fPIC "$project/scenarios/input-client.cpp" -o "$NEST_WORK/input-client" $(pkg-config --cflags --libs Qt6Widgets)
"$NEST_WORK/input-client" krellis-lone --small-min >>"$NEST_OUT/client-a.txt" 2>&1 & a=$!
sleep 2

echo "--- the output is turned, and the share is off: the lone window fills it"
check "the nested output is taller than it is wide" "$(awk -v w="$(geom p1 3)" -v h="$(geom p1 4)" 'BEGIN { print (h > w) }')" 1
config 0
fullw=$(geom p1 3)
fullh=$(geom p1 4)
top=$(geom p1 2)
check "no reading width is applied" "$(lone p1)" 0
check "the camera frames the tile" "$(near "$(cam 1)" 1 0.01)" yes

echo "--- 70% takes rows, not columns"
config 70
check "the width is untouched" "$(near "$(share "$(geom p1 3)" "$fullw")" 1 0.002)" yes
check "the tile takes 70% of the height" "$(near "$(share "$(geom p1 4)" "$fullh")" 0.7 0.02)" yes
want_top=$(awk -v y="$top" -v h="$fullh" 'BEGIN { printf "%.1f", y + 0.15 * h }')
check "and it is centred" "$(near "$(geom p1 2)" "$want_top" 3)" yes
check "the window is marked LONE" "$(lone p1)" 1
check "the camera grew in height only" "$(near "$(cam 2)" "$(awk 'BEGIN { printf "%.4f", 1 / 0.7 }')" 0.01)" yes
check "and not in width" "$(near "$(cam 1)" 1 0.01)" yes
$KRELLIS_ROOT/tools/shot lone-width-portrait >/dev/null 2>&1 || true

echo "--- a second window brings the usual split back (stacked: a turned output splits down its short side)"
"$NEST_WORK/input-client" krellis-other --small-min >>"$NEST_OUT/client-b.txt" 2>&1 & b=$!
sleep 2.5
check "two tiles" "$(call dump | grep -c "^p[12] '")" 2
# A turned output splits down its short side, so the pair stacks instead of standing side by side.
check "the first takes the full width" "$(near "$(share "$(geom p1 3)" "$fullw")" 1 0.002)" yes
check "and half the height" "$(near "$(share "$(geom p1 4)" "$fullh")" 0.5 0.02)" yes
check "the second takes the full width too" "$(near "$(share "$(geom p2 3)" "$fullw")" 1 0.002)" yes
check "and sits below the first" "$(awk -v a="$(geom p1 2)" -v b="$(geom p2 2)" 'BEGIN { print (b > a) }')" 1
check "nothing is marked LONE" "$(lone p1)$(lone p2)" 00
kill "$b"; b=
sleep 2.5

echo "--- closing it brings the share back"
check "one tile again" "$(call dump | grep -c "^p[0-9] '")" 1
check "the height is back to 70%" "$(near "$(share "$(geom p1 4)" "$fullh")" 0.7 0.02)" yes

echo "--- a rule can still opt out"
config 70 'krellis-lone\loneWidth=0'
check "a rule's 0 is full height" "$(near "$(share "$(geom p1 4)" "$fullh")" 1 0.002)" yes
check "and no LONE mark" "$(lone p1)" 0
