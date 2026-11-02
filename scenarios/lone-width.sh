#!/usr/bin/env bash
# A window alone on the canvas takes a configured share of the screen, centred, instead of filling it; a second
# window brings the usual split back, and a rule can override the share for one application.
#
# Widths are compared as shares of the same tile measured with the share off, because a tile is painted inside its
# seam: a 70% tile is 0.7 of the area minus the inset, which is ~12 px on a 1280 px output.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
# Floating point comparison with a tolerance, as shell integer tests cannot see a 3 px seam.
near() { awk -v a="$1" -v b="$2" -v tol="$3" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= tol) }' && echo yes || echo no; }
rect() { call dump | grep "^$1 '" | sed -nE 's/.*screen (-?[0-9.]+),(-?[0-9.]+) ([0-9.]+)x([0-9.]+).*/\1 \2 \3 \4/p'; }
width() { rect "$1" | awk '{print $3}'; }
left() { rect "$1" | awk '{print $1}'; }
camw() { call dump | sed -nE 's/^camera .*\(target [^ ]+ [^ ]+ ([0-9.]+) .*/\1/p'; }
lone() { call dump | grep -c "^$1 '.* LONE"; }
share() { awk -v w="$1" -v f="$2" 'BEGIN { printf "%.4f", w / f }'; }
# [General] with a lone width, plus any rule lines (QSettings group syntax: "app\key=value").
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

echo "--- the share is off by default: a lone window fills the canvas"
config 0
full=$(width p1)
fullh=$(rect p1 | awk '{print $4}')
base_left=$(left p1)
check "the tile is wide" "$(awk -v w="$full" 'BEGIN { print (w > 600) }')" 1
check "no reading width is applied" "$(lone p1)" 0
check "the camera frames the tile" "$(near "$(camw)" 1 0.01)" yes

echo "--- 70% of the width, centred"
config 70
check "the tile takes 70% of the width" "$(near "$(share "$(width p1)" "$full")" 0.7 0.02)" yes
want_left=$(awk -v x="$base_left" -v w="$full" 'BEGIN { printf "%.1f", x + 0.15 * w }')
check "and it is centred" "$(near "$(left p1)" "$want_left" 3)" yes
check "the height is untouched" "$(near "$(rect p1 | awk '{print $4}')" "$fullh" 1)" yes
check "the window is marked LONE" "$(lone p1)" 1
check "the camera widened around it" "$(near "$(camw)" "$(awk 'BEGIN { printf "%.4f", 1 / 0.7 }')" 0.01)" yes
$KRELLIS_ROOT/tools/shot lone-width >/dev/null 2>&1 || true

echo "--- a second window brings the usual split back"
"$NEST_WORK/input-client" krellis-other --small-min >>"$NEST_OUT/client-b.txt" 2>&1 & b=$!
sleep 2.5
check "two tiles" "$(call dump | grep -c "^p[12] '")" 2
check "the first takes half" "$(near "$(share "$(width p1)" "$full")" 0.5 0.02)" yes
check "the second takes half" "$(near "$(share "$(width p2)" "$full")" 0.5 0.02)" yes
check "nothing is marked LONE" "$(lone p1)$(lone p2)" 00
check "the camera frames both" "$(near "$(camw)" 1 0.01)" yes

echo "--- closing it brings the reading width back"
kill "$b"; b=
sleep 2.5
check "one tile again" "$(call dump | grep -c "^p[0-9] '")" 1
check "the width is back to 70%" "$(near "$(share "$(width p1)" "$full")" 0.7 0.02)" yes
check "and it is marked LONE again" "$(lone p1)" 1

echo "--- a rule overrides the general setting"
config 70 'krellis-lone\loneWidth=0'
check "a rule's 0 means full width" "$(near "$(share "$(width p1)" "$full")" 1 0.002)" yes
check "and no LONE mark" "$(lone p1)" 0
config 70 'krellis-lone\loneWidth=55'
check "a rule's 55 wins over the general 70" "$(near "$(share "$(width p1)" "$full")" 0.55 0.02)" yes
check "and it is marked LONE" "$(lone p1)" 1
config 0 'krellis-lone\loneWidth=55'
check "a rule applies even with the general share off" "$(near "$(share "$(width p1)" "$full")" 0.55 0.02)" yes

echo "--- 100 is full width, not a division by zero"
config 100
check "100% is the whole tile" "$(near "$(share "$(width p1)" "$full")" 1 0.002)" yes
check "and no LONE mark" "$(lone p1)" 0
check "the camera is at 1:1" "$(near "$(camw)" 1 0.01)" yes
