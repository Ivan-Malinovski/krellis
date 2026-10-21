#!/usr/bin/env bash
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=/home/ivan/dev/kwin-trellis
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT
c++ -fPIC "$project/scenarios/input-client.cpp" -o "$NEST_WORK/input-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for type in alpha beta gamma duplicate duplicate; do
    "$NEST_WORK/input-client" "persist-$type" --small-min >>"$NEST_OUT/client-$type.txt" 2>&1 & clients="$clients $!"
    sleep 0.3
done
sleep 1
call toggleDock 1; sleep 0.6
call toggleHide 2; sleep 0.4
call focusIndex 0; sleep 1
# The bar is off by default; the save-view button lives on it.
call toggleBar; sleep 1
# Save a named framing of the unique alpha window.
for x in $(seq 12 6 1000); do
    if [ "$(call barHit "$x" 776)" = save ]; then call simButton "$x" 776 true 0; call simButton "$x" 776 false 0; break; fi
done
call simText 'Persistent desk'; call simKey 16777220 true; call simKey 16777220 false
call saveSession | grep true
cp "$XDG_STATE_HOME/kwin-trellis/session.json" "$NEST_OUT/session.json"
python3 - "$NEST_OUT/session.json" <<'PY'
import json,sys
s=open(sys.argv[1]).read(); d=json.loads(s)
assert d['schema']==1
c=d['canvases'][0]
assert len(c['windows'])==5
assert len(c['floating'])==1 and len(c['hidden'])==1
assert len(c['navigation']['framings'])==1
assert 'input-client' not in s and 'caption' not in s and 'pid' not in s
assert sum(w['app']=='persist-duplicate' for w in c['windows'])==2
PY
call dump | tee "$NEST_OUT/saved.txt"
"$project/tools/shot" persist-saved
echo 'PASS atomic snapshot with docked, floating, hidden, named framing and ambiguous identities'
