#!/usr/bin/env bash
# NEST_OUTPUTS=2. Count actual canvas passes during a one-output animation.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL animation-cost scenario exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
c++ -fPIC "$KRELLIS_ROOT/scenarios/lifecycle-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
python3 - "$NEST_WORK/client" "$NEST_OUT" <<'PY'
import json, os, pathlib, re, subprocess, sys, time
client, out = sys.argv[1], pathlib.Path(sys.argv[2])
clients = []
def call(method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + method, *map(str, args)], text=True)
def state(output):
    text = call('dumpOutput', output)
    frames, rest, motion = map(int, re.search(r'PAINT-STATS frames (\d+) rest (\d+) motion (\d+)', text).groups())
    return dict(frames=frames, rest=rest, motion=motion,
        requests=int(re.search(r'NATIVE-REQUESTS (\d+)', text)[1]))
def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    assert ok, label
try:
    for name in ('cost-alpha', 'cost-beta', 'cost-other'):
        clients.append(subprocess.Popen([client, name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        time.sleep(.25)
    call('selectOutput', 0)
    call('moveToOutput', 2, 1)
    time.sleep(float(os.environ.get('KRELLIS_COST_WARMUP', '1')))
    before = [state(i) for i in (0, 1)]
    call('toggleMaximize', 0)
    time.sleep(.08) # let the single destination configure and its commit settle
    start = [state(i) for i in (0, 1)]
    time.sleep(.25)
    end = [state(i) for i in (0, 1)]
    delta = [{k: b[k] - a[k] for k in a} for a, b in zip(start, end)]
    result = dict(before=before, start=start, end=end, delta=delta)
    (out / 'cost.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result), flush=True)
    check(delta[0]['frames'] >= 3, 'moving output renders animation frames')
    check(delta[0]['requests'] == 0, 'animation adds no native configure requests')
    if os.environ.get('KRELLIS_COST_BASELINE') != '1':
        check(delta[0]['rest'] == 0 and delta[0]['motion'] == 0,
              'settled destination skips geometry and motion scans during paint animation')
        check(delta[1]['frames'] <= 2 and delta[1]['frames'] < delta[0]['frames'],
              'idle output avoids animation-driven canvas passes')
    time.sleep(.5)
    idle_before = [state(i) for i in (0, 1)]
    time.sleep(.2)
    idle_after = [state(i) for i in (0, 1)]
    check(idle_before == idle_after, 'both outputs become idle after the animation')
    call('selectOutput', 0)
    call('toggleMaximize', 0)
    time.sleep(.6)
    check('ANIMATION profile hyprland' in call('dumpOutput', 0), 'profile remains active')
finally:
    for p in clients:
        if p.poll() is None: p.terminate()
    for p in clients: p.wait(timeout=5)
PY
