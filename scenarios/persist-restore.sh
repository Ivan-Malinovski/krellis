#!/usr/bin/env bash
# Seed with NEST_SESSION_FILE=<persist-save/out/session.json> before KWin starts.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT
test -s "$XDG_STATE_HOME/krellis/session.json"
c++ -fPIC "$project/scenarios/input-client.cpp" -o "$NEST_WORK/input-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
# Reversed arrivals deliberately give every unique window a different local ID.
for type in gamma beta alpha duplicate duplicate; do
    "$NEST_WORK/input-client" "persist-$type" --small-min >>"$NEST_OUT/client-$type.txt" 2>&1 & clients="$clients $!"
    sleep 0.2
done
sleep 2.2
call sessionStatus | tee "$NEST_OUT/status.txt" | grep -Fx "SESSION complete 1 matched 3 ambiguous 2 pending 0 error ''"
call dump | tee "$NEST_OUT/restored.txt"
call dump | grep "app 'persist-beta'.* FLOAT"
call dump | grep "^FRAMING .* 'Persistent desk'$"
call dump | grep "^framed 'p3'$"
# Gamma is hidden, and the two ambiguous duplicates stayed in their fresh panels.
call dump | grep "^p1 .*app 'persist-gamma'"
call dump | grep "^ORDER .*p4.*p5"
! call dump | grep "^p[45] .* FLOAT"
! call dump | grep "^ORDER .*p1"
"$project/tools/shot" persist-restored
# The persisted hidden restore target must still work after panel-ID remapping.
call toggleHide 0; sleep 0.8
# Its original neighbour was an ambiguous duplicate, so restoration uses the
# model's safe floating fallback rather than guessing that neighbour.
call dump | tee "$NEST_OUT/unhidden.txt" | grep "app 'persist-gamma'.* FLOAT"
call saveSession | grep true
python3 - "$XDG_STATE_HOME/krellis/session.json" <<'PY'
import json,sys
c=json.load(open(sys.argv[1]))['canvases'][0]
assert len(c['windows'])==5 and len(c['hidden'])==0
assert sum(w['app']=='persist-duplicate' for w in c['windows'])==2
PY
echo 'PASS restart restores only unique identities with remapped hidden targets and saved framing'
