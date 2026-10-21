#!/usr/bin/env bash
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=/home/ivan/dev/kwin-trellis
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
cat >"$XDG_CONFIG_HOME/trellisrc" <<'RULES'
[ViewRules]
trellis-input-fixture\minWidth=640
trellis-input-fixture\minHeight=1200
RULES
call reloadRules
c++ -fPIC "$project/scenarios/input-client.cpp" -o "$NEST_WORK/input-client" $(pkg-config --cflags --libs Qt6Widgets)
"$NEST_WORK/input-client" >"$NEST_OUT/client.txt" 2>&1 & client=$!
trap 'kill "$client" 2>/dev/null || true; pkill -P "$$" -x alacritty || true' EXIT
sleep 1
for n in 1 2; do alacritty --title "scale-$n" -e sleep 600 >/dev/null 2>&1 & sleep 0.3; done
sleep 1
call overview; sleep 1
# Convert a desired client-local point to its current painted position.
point() {
    call dump >"$NEST_OUT/point.txt"
    python3 - "$NEST_OUT/point.txt" "$1" "$2" "${3:-main}" <<'PY'
import sys,re
lines=open(sys.argv[1]).read().splitlines(); popup=sys.argv[4]=='popup'
s=next(s for s in lines if (s.startswith('POPUP ') if popup else "'input-client' screen " in s))
def rect(key): return tuple(map(float,re.search(key+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',s).groups()))
x,y,w,h=rect('screen'); fx,fy,fw,fh=rect('frame'); bx,by,_,_=rect('frame' if popup else 'client')
u,v=float(sys.argv[2]),float(sys.argv[3]); print(x+(bx+u-fx)*w/fw, y+(by+v-fy)*h/fh)
PY
}
received() {
    python3 - "$NEST_OUT/client.txt" "$1" "$2" "$3" <<'PYTEST'
import sys
lines=[l.split() for l in open(sys.argv[1]) if l.startswith(sys.argv[2]+' ')]
assert lines, (sys.argv[2], open(sys.argv[1]).read())
x,y=map(float,lines[-1][1:3]); expected=list(map(float,sys.argv[3:5]))
assert abs(x-expected[0])<.3 and abs(y-expected[1])<.3, (x,y,expected)
print('received',*lines[-1])
PYTEST
}
click() { call simMotion "$1" "$2"; call simButton "$1" "$2" true 0; call simButton "$1" "$2" false 0; sleep 0.2; }
read -r x y < <(point 120 100)
python3 - "$NEST_OUT/point.txt" <<'PYTEST'
import sys,re
line=next(l for l in open(sys.argv[1]) if "input-client" in l)
m=re.search(r'screen [-\d.]+,[-\d.]+ ([\d.]+)x[\d.]+ frame [-\d.]+,[-\d.]+ ([\d.]+)x',line)
assert float(m[1])/float(m[2]) < .99, line
PYTEST
click "$x" "$y"
received CLICK 120 100
received RELEASE 120 100
# Pointer capture maps out-of-bounds motion/release to the original client.
call simButton "$x" "$y" true 0
read -r outsideX outsideY < <(point 1400 100)
call simMotion "$outsideX" "$outsideY"
call simButton "$outsideX" "$outsideY" false 0; sleep 0.2
received RELEASE 1400 100
call simButton "$x" "$y" true 0
read -r outsideX outsideY < <(point -400 100)
call simMotion "$outsideX" "$outsideY"
call simButton "$outsideX" "$outsideY" false 0; sleep 0.2
received RELEASE -400 100
call simAxis -15 "$x" "$y" 0; sleep 0.2
received WHEEL 120 100
# Gesture input is inert even when a scaled client is visibly under the pointer.
before=$(grep -c '^CLICK ' "$NEST_OUT/client.txt")
call simPinch 1.15 640 350
click "$x" "$y"
test "$before" = "$(grep -c '^CLICK ' "$NEST_OUT/client.txt")"
call simPinchEnd; sleep 1
call overview; sleep 1
# Open a grabbed xdg popup and click it at coordinates derived from its paint rect.
read -r x y < <(point 20 20)
click "$x" "$y"; sleep 0.3
call dump | tee "$NEST_OUT/popup.txt" | grep '^POPUP '
python3 - "$NEST_OUT/popup.txt" <<'PYTEST'
import sys,re
line=next(l for l in open(sys.argv[1]) if l.startswith('POPUP '))
m=re.search(r'screen [-\d.]+,[-\d.]+ ([\d.]+)x[\d.]+ frame [-\d.]+,[-\d.]+ ([\d.]+)x',line)
assert float(m[1])/float(m[2]) < .99, line
PYTEST
read -r px py < <(point 80 40 popup)
click "$px" "$py"
received POP_CLICK 80 40
"$project/tools/shot" scaled-popup
# Clicking outside dismisses the popup without sending a stray release to the body.
read -r x y < <(point 180 300)
click "$x" "$y"; sleep 0.3
! call dump | grep -q '^POPUP '
echo 'PASS scaled motion, click, capture, wheel, gesture inertness, popup mapping/dismissal'
