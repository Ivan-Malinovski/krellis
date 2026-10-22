#!/usr/bin/env bash
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT
cat >"$XDG_CONFIG_HOME/krellisrc" <<'RULES'
[ViewRules]
krellis-stage\placement=stage
krellis-stage\allowFloating=false
krellis-float\placement=floating
krellis-inert\scaling=inert
krellis-inert\minWidth=800
krellis-inert\minHeight=400
krellis-none\scaling=false
krellis-single\singleton=true
krellis-single\closable=false
RULES
call reloadRules
c++ -fPIC "$project/scenarios/input-client.cpp" -o "$NEST_WORK/input-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for type in stage float inert none single single; do
    "$NEST_WORK/input-client" "krellis-$type" --small-min >>"$NEST_OUT/client-$type.txt" 2>&1 & clients="$clients $!"
    sleep 0.5
done
sleep 1
call overview; sleep 1
call dump | tee "$NEST_OUT/initial.txt"
test "$(call dump | grep -c ' screen ')" -eq 5
test "$(call dump | grep -c "app 'krellis-single'")" -eq 1
call dump | grep "app 'krellis-float'.* FLOAT"
call dump | grep "app 'krellis-inert'.* INERT"
call dump | grep "app 'krellis-none'.* NO_SCALE"
call dump | grep "app 'krellis-single'.* NO_CLOSE"
# Disallowed floating is refused. Float->dock->float remembers exact viewport rect.
call toggleDock 0; sleep 0.5
! call dump | grep "app 'krellis-stage'.* FLOAT"
floatRect=$(call dump | grep "app 'krellis-float'" | sed 's/.*screen \([^ ]* [^ ]*\) frame.*/\1/')
call toggleDock 1; sleep 1
! call dump | grep "app 'krellis-float'.* FLOAT"
call toggleDock 1; sleep 1
call dump | grep "app 'krellis-float'.* FLOAT"
test "$floatRect" = "$(call dump | grep "app 'krellis-float'" | sed 's/.*screen \([^ ]* [^ ]*\) frame.*/\1/')"
# Reload a changed allowSide rule; it must also block toggleDock.
python3 - "$XDG_CONFIG_HOME/krellisrc" <<'PYRULE'
from pathlib import Path
import sys
p=Path(sys.argv[1]); p.write_text(p.read_text().replace(r'krellis-float\placement=floating', 'krellis-float\\placement=floating\nkrellis-float\\allowSide=false'))
PYRULE
call reloadRules; sleep 0.6
call toggleDock 1; sleep 0.5
call dump | grep "app 'krellis-float'.* FLOAT"
# Inert content must not receive a press; interactive reload restores delivery.
point() {
    call dump >"$NEST_OUT/point.txt"
    python3 - "$NEST_OUT/point.txt" "$1" <<'PY'
import sys,re
s=next(l for l in open(sys.argv[1]) if "app 'krellis-"+sys.argv[2]+"'" in l)
def rect(key): return tuple(map(float,re.search(key+r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)',s).groups()))
x,y,w,h=rect('screen');fx,fy,fw,fh=rect('frame');bx,by,_,_=rect('client')
print(x+(bx+100-fx)*w/fw,y+(by+100-fy)*h/fh)
PY
}
call overview; sleep 1
# Keep the float from occluding the inert panel while checking content delivery.
call toggleHide 1; sleep 0.5
read -r x y < <(point inert)
call simButton "$x" "$y" true 0; call simButton "$x" "$y" false 0; sleep 0.2
! grep -q '^CLICK ' "$NEST_OUT/client-inert.txt"
sed -i 's/scaling=inert/scaling=interactive/' "$XDG_CONFIG_HOME/krellisrc"
call reloadRules; sleep 0.6
read -r x y < <(point inert)
call simButton "$x" "$y" true 0; call simButton "$x" "$y" false 0; sleep 0.2
grep '^CLICK ' "$NEST_OUT/client-inert.txt"
# Activate the protected singleton, then invoke the effect's close command.
read -r x y < <(point single)
call simButton "$x" "$y" true 0; call simButton "$x" "$y" false 0
call closeActive; sleep 0.4
call dump | grep "app 'krellis-single'.* NO_CLOSE"
"$project/tools/shot" view-rules
echo 'PASS initial placement, singleton, close guard, allow rules, inert/reload and remembered docking'
