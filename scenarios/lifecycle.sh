#!/usr/bin/env bash
# Enable/disable the effect only inside tools/nested.sh. Native inspection uses
# KWin scripting, so assertions still work after /Trellis has been unregistered.
set -euo pipefail
test "${NEST_SOCKET:-}" = trellis-nest
test -S "$XDG_RUNTIME_DIR/$NEST_SOCKET"
test "${XDG_CONFIG_HOME:-}" = "$NEST_WORK/config"
test "${XDG_STATE_HOME:-}" = "$NEST_WORK/state"
test -n "${DBUS_SESSION_BUS_ADDRESS:-}"
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
project=/home/ivan/dev/kwin-trellis
call() { qdbus6 org.kde.KWin /Trellis org.kde.kwin.KWin.TrellisEffect."$@"; }
effect() { qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects."$@"; }
trap 'for process in ${clients:-}; do kill "$process" 2>/dev/null || true; done' EXIT

script() {
    local label=$1 id
    cat >"$NEST_WORK/$label.js"
    id=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/$label.js" "trellis-lifecycle-$label")
    qdbus6 org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script.run
}
snapshot() {
    local label=$1 attempt
    script "$label" <<JS
var rows = workspace.windowList().filter(function(w) { return w.caption.indexOf('lifecycle-') === 0; }).map(function(w) {
    var r = w.frameGeometry;
    return {name:w.caption, x:r.x, y:r.y, w:r.width, h:r.height, output:w.output ? w.output.name : '', above:w.keepAbove, below:w.keepBelow, minimized:w.minimized, move:w.move, resize:w.resize};
});
console.warn('TRELLIS_NATIVE_$label ' + JSON.stringify(rows));
JS
    for attempt in $(seq 1 60); do
        if python3 - "$NEST_OUT/kwin.log" "$NEST_OUT/$label.json" "$label" <<'PY'
import json, sys
marker = 'TRELLIS_NATIVE_' + sys.argv[3] + ' '
for line in open(sys.argv[1]):
    if marker in line:
        value = json.loads(line.split(marker, 1)[1])
        json.dump(value, open(sys.argv[2], 'w'))
        sys.exit(0)
sys.exit(1)
PY
        then return; fi
        sleep 0.05
    done
    echo "No native snapshot $label"; exit 1
}
indexOf() {
    local output=$1 name=$2 snapshot
    snapshot=$(call dumpOutput "$output")
    awk -v wanted="'$name'" '/ screen / { if (!found && index($0,wanted)) {print n+0; found=1} n++ }' <<<"$snapshot"
}

effect unloadEffect trellis
c++ -fPIC "$project/scenarios/lifecycle-client.cpp" -o "$NEST_WORK/lifecycle-client" $(pkg-config --cflags --libs Qt6Widgets)
clients=
for app in alpha above hidden minimized closed; do
    "$NEST_WORK/lifecycle-client" "lifecycle-$app" >"$NEST_OUT/client-$app.txt" 2>&1 & clients="$clients $!"
    if [ "$app" = closed ]; then closed=$!; fi
    sleep 0.2
done
sleep 0.5
script setup <<'JS'
var names = ['alpha','above','hidden','minimized','closed'];
workspace.windowList().forEach(function(w) {
    var i = names.indexOf(w.caption.replace('lifecycle-', ''));
    if (i < 0) return;
    w.frameGeometry = {x:60+i*35, y:70+i*24, width:480+i*12, height:320+i*10};
    w.keepAbove = i === 1;
    w.keepBelow = i === 0;
});
JS
# Settle the visible configure before minimizing. A minimized Wayland client
# may legitimately defer a pending size, which would make the baseline ambiguous.
settled=false
for attempt in $(seq 1 40); do
    snapshot "prepared-$attempt"
    if python3 - "$NEST_OUT/prepared-$attempt.json" <<'PY'
import json,sys
names=['alpha','above','hidden','minimized','closed']
windows={w['name']:w for w in json.load(open(sys.argv[1]))}
if len(windows)!=len(names): sys.exit(1)
for i,name in enumerate(names):
    w=windows['lifecycle-'+name]
    expected=dict(x=60+i*35,y=70+i*24,w=480+i*12,h=320+i*10)
    if any(abs(w[key]-value)>1 for key,value in expected.items()): sys.exit(1)
    if w['minimized']: sys.exit(1)
PY
    then settled=true; break; fi
    sleep 0.05
done
"$settled"
script minimize <<'JS'
workspace.windowList().forEach(function(w) {
    if (w.caption === 'lifecycle-minimized') w.minimized = true;
});
JS
sleep 0.3
snapshot baseline
python3 - "$NEST_OUT/prepared-$attempt.json" "$NEST_OUT/baseline.json" <<'PY'
import json,sys
prepared={w['name']:w for w in json.load(open(sys.argv[1]))}
baseline={w['name']:w for w in json.load(open(sys.argv[2]))}
assert prepared.keys()==baseline.keys()
for name,w in baseline.items():
    assert w['minimized']==(name=='lifecycle-minimized'), w
    for key in ('x','y','w','h'):
        assert abs(w[key]-prepared[name][key])<=1, (name,key,prepared[name],w)
PY
effect loadEffect trellis | grep true
sleep 1.5

# Moving between canvases must not replace the global native baseline.
hidden_output=0
# Output names depend on the backend: WL-n nested in a window, Virtual-n off-screen.
output_prefix=WL; [ "${NEST_BACKEND:-wayland}" = virtual ] && output_prefix=Virtual
if [ "${NEST_OUTPUTS:-1}" -eq 2 ]; then
    call selectOutput 0
    call moveToOutput "$(indexOf 0 lifecycle-alpha)" 1
    call selectOutput 0
    call moveToOutput "$(indexOf 0 lifecycle-hidden)" 1
    hidden_output=1
    sleep 0.6
fi
call selectOutput 0
call toggleDock "$(indexOf 0 lifecycle-above)"
call selectOutput "$hidden_output"
call toggleHide "$(indexOf "$hidden_output" lifecycle-hidden)"
sleep 0.5
settled=false
for attempt in $(seq 1 40); do
    snapshot "hidden-buffer-$attempt"
    if python3 - "$NEST_OUT/baseline.json" "$NEST_OUT/hidden-buffer-$attempt.json" "${output_prefix}-$hidden_output" <<'PY'
import json,sys
before=next(w for w in json.load(open(sys.argv[1])) if w['name']=='lifecycle-hidden')
actual=next(w for w in json.load(open(sys.argv[2])) if w['name']=='lifecycle-hidden')
if not actual['minimized']: sys.exit(1)
if actual['output']!=sys.argv[3]: sys.exit(1)
if any(abs(actual[key]-before[key])>1 for key in ('w','h')): sys.exit(1)
PY
    then settled=true; break; fi
    sleep 0.05
done
if ! "$settled"; then
    echo 'Hide flight did not commit the baseline native buffer'
    cat "$NEST_OUT/hidden-buffer-$attempt.json"
    exit 1
fi
call dumpOutput "$hidden_output" >"$NEST_OUT/hidden-owner.txt"
grep "app 'lifecycle-hidden'" "$NEST_OUT/hidden-owner.txt"
if [ "${NEST_OUTPUTS:-1}" -eq 2 ]; then
    call dumpOutput 0 >"$NEST_OUT/hidden-other-output.txt"
    ! grep "app 'lifecycle-hidden'" "$NEST_OUT/hidden-other-output.txt"
fi
kill "$closed"; wait "$closed" || true
call selectOutput 0
call focusIndex "$(indexOf 0 lifecycle-above)"
sleep 0.4
# Disable with a live docking preview, then verify its cancellation and cleanup.
call simDragStart "$(indexOf 0 lifecycle-above)" 300 180 | grep true
call simDragMove 800 400
sleep 0.2
effect unloadEffect trellis
sleep 0.8
snapshot disabled
python3 - "$NEST_OUT/baseline.json" "$NEST_OUT/disabled.json" <<'PY'
import json,sys
before = {w['name']:w for w in json.load(open(sys.argv[1]))}
after = {w['name']:w for w in json.load(open(sys.argv[2]))}
assert set(after) == set(before) - {'lifecycle-closed'}, after
for name, actual in after.items():
    expected = before[name]
    for key in ('x','y','w','h'):
        assert abs(actual[key] - expected[key]) <= 1, (name,key,expected,actual)
    assert actual['output'] == expected['output'], (name,expected,actual)
    assert actual['above'] == expected['above'], (name,expected,actual)
    assert actual['below'] == expected['below'], (name,expected,actual)
    assert actual['minimized'] == (name in ('lifecycle-hidden','lifecycle-minimized')), (name,actual)
    assert not actual['move'] and not actual['resize'], actual
PY

# Independently restoring the hidden client after unload must retain the exact
# original geometry too, without any remaining effect code or delayed callbacks.
script reveal-hidden <<'JS'
workspace.windowList().forEach(function(w) {
    if (w.caption === 'lifecycle-hidden') w.minimized = false;
});
JS
settled=false
for attempt in $(seq 1 40); do
    snapshot "revealed-$attempt"
    if python3 - "$NEST_OUT/baseline.json" "$NEST_OUT/revealed-$attempt.json" <<'PY'
import json,sys
before={w['name']:w for w in json.load(open(sys.argv[1])) if w['name']!='lifecycle-closed'}
after={w['name']:w for w in json.load(open(sys.argv[2]))}
if before.keys()!=after.keys(): sys.exit(1)
for name,w in after.items():
    if any(abs(w[key]-before[name][key])>1 for key in ('x','y','w','h')): sys.exit(1)
    if w['minimized']!=(name=='lifecycle-minimized'): sys.exit(1)
    if any(w[key]!=before[name][key] for key in ('output','above','below','move','resize')): sys.exit(1)
PY
    then settled=true; break; fi
    sleep 0.05
done
if ! "$settled"; then
    echo 'Native user restore did not commit the original geometry'
    cat "$NEST_OUT/baseline.json" "$NEST_OUT/revealed-$attempt.json"
    exit 1
fi
script rehide <<'JS'
workspace.windowList().forEach(function(w) {
    if (w.caption === 'lifecycle-hidden') w.minimized = true;
});
JS
sleep 0.2
snapshot disabled-settled

# A second enable/disable must capture the restored state rather than the old
# effect's transient geometry. Remove this scenario's saved state between loads.
test "${XDG_STATE_HOME:-}" = "$NEST_WORK/state"
rm -f "$XDG_STATE_HOME/kwin-trellis/session.json"
effect loadEffect trellis | grep true
sleep 1
script native-move <<'JS'
var alpha = workspace.windowList().filter(function(w) { return w.caption === 'lifecycle-alpha'; })[0];
workspace.activeWindow = alpha;
workspace.slotWindowMove();
JS
sleep 0.2
snapshot moving
python3 - "$NEST_OUT/moving.json" <<'PY'
import json,sys
alpha = next(w for w in json.load(open(sys.argv[1])) if w['name']=='lifecycle-alpha')
assert alpha['move'], alpha
PY
effect unloadEffect trellis
sleep 0.8
snapshot reloaded
python3 - "$NEST_OUT/disabled-settled.json" "$NEST_OUT/reloaded.json" <<'PY'
import json,sys
before = {w['name']:w for w in json.load(open(sys.argv[1]))}
after = {w['name']:w for w in json.load(open(sys.argv[2]))}
assert before.keys() == after.keys()
for name,actual in after.items():
    expected=before[name]
    for key in ('x','y','w','h'):
        assert abs(actual[key]-expected[key]) <= 1, (name,key,expected,actual)
    for key in ('output','above','below','minimized','move','resize'):
        assert actual[key] == expected[key], (name,key,expected,actual)
PY

# Unload during a real native resize must cancel the grab and restore baseline.
rm -f "$XDG_STATE_HOME/kwin-trellis/session.json"
effect loadEffect trellis | grep true
sleep 1
alphaIndex=$(indexOf 0 lifecycle-alpha)
call selectOutput 0
call simNativeResizeStart "$alphaIndex" 1 1 | grep -x true
call simNativeResizeMove 30 25 | grep -x true
sleep 0.2
snapshot resizing
python3 - "$NEST_OUT/resizing.json" <<'PYTEST'
import json,sys
alpha=next(w for w in json.load(open(sys.argv[1])) if w['name']=='lifecycle-alpha')
assert alpha['resize'],alpha
PYTEST
effect unloadEffect trellis
sleep 0.8
snapshot resize-unloaded
python3 - "$NEST_OUT/disabled-settled.json" "$NEST_OUT/resize-unloaded.json" <<'PYTEST'
import json,sys
before={w['name']:w for w in json.load(open(sys.argv[1]))}
after={w['name']:w for w in json.load(open(sys.argv[2]))}
assert before.keys()==after.keys()
for name,actual in after.items():
    expected=before[name]
    for key in ('x','y','w','h'):
        assert abs(actual[key]-expected[key])<=1,(name,key,expected,actual)
    for key in ('output','above','below','minimized','move','resize'):
        assert actual[key]==expected[key],(name,key,expected,actual)
PYTEST

# Singleton suppression is automatic policy, and must not leave an extra
# minimized native window behind when the effect is disabled.
cat >"$XDG_CONFIG_HOME/trellisrc" <<'INI'
[ViewRules]
lifecycle-duplicate\singleton=true
INI
for name in first second; do
    "$NEST_WORK/lifecycle-client" lifecycle-duplicate "lifecycle-duplicate-$name" >"$NEST_OUT/client-duplicate-$name.txt" 2>&1 & clients="$clients $!"
    sleep 0.2
done
sleep 0.4
snapshot singleton-before
rm -f "$XDG_STATE_HOME/kwin-trellis/session.json"
effect loadEffect trellis | grep true
sleep 1
snapshot singleton-active
python3 - "$NEST_OUT/singleton-active.json" <<'PY'
import json,sys
windows=[w for w in json.load(open(sys.argv[1])) if w['name'].startswith('lifecycle-duplicate-')]
assert len(windows)==2 and sum(w['minimized'] for w in windows)==1, windows
PY
effect unloadEffect trellis
sleep 0.8
snapshot singleton-after
python3 - "$NEST_OUT/singleton-before.json" "$NEST_OUT/singleton-after.json" <<'PY'
import json,sys
before={w['name']:w for w in json.load(open(sys.argv[1]))}
after={w['name']:w for w in json.load(open(sys.argv[2]))}
assert before.keys()==after.keys()
for name,actual in after.items():
    expected=before[name]
    for key in ('x','y','w','h'):
        assert abs(actual[key]-expected[key])<=1, (name,key,expected,actual)
    for key in ('output','above','below','minimized','move','resize'):
        assert actual[key]==expected[key], (name,key,expected,actual)
PY
echo 'PASS native geometry, above/below, user/automatic minimization, closed windows, transfer, cancel and reload lifecycle'
