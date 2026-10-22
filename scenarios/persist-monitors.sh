#!/usr/bin/env bash
# Run twice with NEST_OUTPUTS=2; seed the second run with the first out/session.json.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=$KRELLIS_ROOT
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT
test "$NEST_OUTPUTS" -eq 2
restoring=false
if [ -n "${NEST_SESSION_FILE:-}" ]; then restoring=true; fi
c++ -fPIC "$project/scenarios/input-client.cpp" -o "$NEST_WORK/input-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
types='alpha beta gamma delta epsilon zeta'
if "$restoring"; then types='zeta epsilon delta gamma beta alpha'; fi
for type in $types; do
    "$NEST_WORK/input-client" "persist-monitor-$type" --small-min >>"$NEST_OUT/client-$type.txt" 2>&1 & clients="$clients $!"
    sleep 0.2
done
sleep 2.2
call toggleBar >/dev/null # the bar is off by default; one call covers every output

indexOf() {
    call dumpOutput "$1" | awk -v app="'persist-monitor-$2'" '/ screen / { if (index($0,app)) { print n+0; exit } n++ }'
}
place() {
    local app=$1 destination=$2 source index
    for source in 0 1; do
        index=$(indexOf "$source" "$app")
        if [ -n "$index" ]; then
            call selectOutput "$source"; call moveToOutput "$index" "$destination"; return
        fi
    done
    echo "Missing app $app"; exit 1
}
saveView() {
    local output=$1 name=$2 x
    for x in $(seq $((output*1280+12)) 6 $((output*1280+1200))); do
        if [ "$(call barHit "$x" 776)" = save ]; then
            call simButton "$x" 776 true 0; call simButton "$x" 776 false 0
            call simText "$name"; call simKey 16777220 true; call simKey 16777220 false
            return
        fi
    done
    echo "Missing Save on output $output"; exit 1
}

if "$restoring"; then
    call sessionStatus | tee "$NEST_OUT/status.txt" | grep -Fx "SESSION complete 1 matched 6 ambiguous 0 pending 0 error ''"
else
    for app in alpha beta gamma; do place "$app" 0; done
    for app in delta epsilon zeta; do place "$app" 1; done
    call selectOutput 0
    call toggleDock "$(indexOf 0 beta)"; sleep 0.6
    call focusIndex "$(indexOf 0 gamma)"; sleep 0.6
    saveView 0 'Left workspace'
    call selectOutput 1
    call toggleHide "$(indexOf 1 zeta)"; sleep 0.5
    call focusIndex "$(indexOf 1 delta)"; sleep 0.6
    saveView 1 'Right workspace'
fi
sleep 0.5
for output in 0 1; do
    call dumpOutput "$output" >"$NEST_OUT/output-$output.txt"
    test "$(grep -c ' screen ' "$NEST_OUT/output-$output.txt")" -eq 3
done
call saveSession | grep true
cp "$XDG_STATE_HOME/krellis/session.json" "$NEST_OUT/session.json"

python3 - "$NEST_OUT/session.json" "${NEST_SESSION_FILE:-}" <<'PY'
import json, math, sys
current = json.load(open(sys.argv[1]))
assert len(current['canvases']) == 2
all_apps = []
for c in current['canvases']:
    apps = {w['app'] for w in c['windows']}
    all_apps.extend(apps)
    assert len(apps) == 3
    assert len(c['navigation']['framings']) == 1
    name = c['navigation']['framings'][0]['name']
    if name == 'Left workspace':
        assert apps == {'persist-monitor-' + s for s in ('alpha','beta','gamma')}
        assert len(c['floating']) == 1 and not c['hidden']
    else:
        assert name == 'Right workspace'
        assert apps == {'persist-monitor-' + s for s in ('delta','epsilon','zeta')}
        assert not c['floating'] and len(c['hidden']) == 1
assert len(set(all_apps)) == 6

def canonical(c):
    identities = {w['panel']: (w['app'], w['role']) for w in c['windows']}
    def identifier(i):
        return identities.get(i, i)
    def tree(n):
        if n is None: return None
        if n['kind'] == 'panel': return ('panel', identifier(n['id']))
        if n['kind'] == 'stage': return ('stage', tree(n['child']))
        return ('split', n['axis'], [round(w/sum(n['weights']), 9) for w in n['weights']], [tree(x) for x in n['children']])
    def hidden(h):
        r = h['restore']
        return (tree(h['panel']), r['kind'], identifier(r['beside']), identifier(r['panel']), r['edge'], r['share'], r['layer'], r['rect'])
    return (tree(c['root']),
            [(tree(f['panel']),f['rect'],f['z'],f['layer']) for f in c['floating']],
            [hidden(h) for h in c['hidden']],
            [identifier(i) for i in c['navigation']['frame']],
            [(f['id'],f['name'],[identifier(i) for i in f['frame']]) for f in c['navigation']['framings']])
if sys.argv[2]:
    previous = json.load(open(sys.argv[2]))
    assert {c['output']: canonical(c) for c in current['canvases']} == {c['output']: canonical(c) for c in previous['canvases']}
PY

if "$restoring"; then
    # The hidden right-hand panel restores beside its remapped live neighbour.
    call selectOutput 1
    call toggleHide "$(indexOf 1 zeta)"; sleep 0.8
    call dumpOutput 1 | tee "$NEST_OUT/unhidden.txt" | grep "app 'persist-monitor-zeta'"
    ! call dumpOutput 1 | grep "app 'persist-monitor-zeta'.* FLOAT"
    call saveSession | grep true
    python3 - "$XDG_STATE_HOME/krellis/session.json" <<'PY'
import json, sys
canvases = json.load(open(sys.argv[1]))['canvases']
assert all(not c['hidden'] for c in canvases)
assert sum(len(c['floating']) for c in canvases) == 1
PY
fi
"$project/tools/shot" persist-monitors
echo 'PASS independent output persistence, reordered arrivals, ratios, floats, hidden anchors and saved views'
