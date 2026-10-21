#!/usr/bin/env bash
# Verify replacements capture actual paint geometry and opacity, not layout targets.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT
project=/home/ivan/dev/kwin-trellis
c++ -fPIC "$project/scenarios/lifecycle-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for app in alpha beta; do
    "$NEST_WORK/client" "interrupt-$app" >"$NEST_OUT/client-$app.txt" 2>&1 &
    clients="$clients $!"
    if [ "$app" = alpha ]; then alpha=$!; else beta=$!; fi
    sleep 0.4
done
sleep 1
call toggleHide 0
sleep 0.07
call dump >"$NEST_OUT/hide.txt"
call toggleHide 0
call dump >"$NEST_OUT/restore.txt"
call reloadRules
call dump >"$NEST_OUT/rules-rebase.txt"
sleep 0.07
call toggleHide 0
call dump >"$NEST_OUT/rehide.txt"
call toggleHide 0
sleep 0.6
call toggleDock 0
sleep 0.07
call dump >"$NEST_OUT/dock.txt"
call toggleDock 0
call dump >"$NEST_OUT/redock.txt"
sleep 0.07
call dump >"$NEST_OUT/preclose.txt"
kill "$alpha"
sleep 0.025
call dump >"$NEST_OUT/close.txt"
sleep 0.2
call toggleHide 0
sleep 0.07
kill "$beta"
sleep 0.025
call dump >"$NEST_OUT/hidden-close.txt"
python3 - "$NEST_OUT" <<'PY'
import pathlib,re,sys
root=pathlib.Path(sys.argv[1])
number=r'(-?[\d.]+)'
rect=number+','+number+' '+number+'x'+number

def row(name):
    text=(root/(name+'.txt')).read_text()
    return next(l for l in text.splitlines() if "'interrupt-alpha'" in l)
def values(line,tag):
    m=re.search(tag+' '+rect,line)
    assert m,(tag,line)
    return tuple(map(float,m.groups()))
def opacity(line,tag):
    return float(re.search(tag+r' ([\d.]+)',line)[1])
hide=row('hide'); restore=row('restore'); rehide=row('rehide')
# A hide reversed early starts at its current flight, with fractional opacity.
assert 0.1 < opacity(restore,'baseOpacity') < 0.95,restore
assert values(restore,'from')[2] > 100,restore
assert 0.1 < opacity(rehide,'baseOpacity') < 1,rehide
assert 0.1 < opacity(row('rules-rebase'),'baseOpacity') < .99,row('rules-rebase')
# Dock reversed during its glide must not start at the already committed float.
dock=row('dock'); redock=row('redock')
paint=values(dock,'FLY'); source=values(redock,'from'); target=values(dock,'screen')
assert max(abs(a-b) for a,b in zip(paint,source)) < 160,(paint,source)
assert max(abs(a-b) for a,b in zip(target,source)) > 15,(target,source)
# Closing a glide retains its visual source, rather than its layout target.
closing=next(l for l in (root/'close.txt').read_text().splitlines() if l.startswith('CLOSING interrupt-alpha'))
source=values(closing,'from'); previous=values(row('preclose'),'FLY')
assert max(abs(a-b) for a,b in zip(source,previous)) < 130,(source,previous)
hidden=next(l for l in (root/'hidden-close.txt').read_text().splitlines() if l.startswith('CLOSING interrupt-beta'))
assert 0.1 < opacity(hidden,'baseOpacity') < .95,hidden
print('PASS interrupted hide/restore opacity, dock reversal and close paint capture')
PY
