#!/usr/bin/env bash
# Real-frame icon pixels survive navigation away and back, including delegate resets.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL icons-reentry exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
c++ -O2 -fPIC "$KRELLIS_ROOT/scenarios/zoom-load-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
python3 - <<'PY'
import json, os, pathlib, re, subprocess, time
out = pathlib.Path(os.environ['NEST_OUT'])
def call(method, *args, probe=False):
    path, interface = ('/KrellisPaintProbe', 'org.kde.kwin.KrellisPaintProbe') if probe else ('/Krellis', 'org.kde.kwin.KWin.KrellisEffect')
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', path, interface+'.'+method, *map(str,args)], text=True).strip()
def check(ok, label):
    print(('PASS ' if ok else 'FAIL ')+label, flush=True)
    assert ok, label
client = subprocess.Popen([os.environ['NEST_WORK']+'/client','25'], stdout=subprocess.DEVNULL)
try:
    for _ in range(100):
        if len(re.findall(r'^p\d+ ', call('dump'), re.M)) == 25: break
        time.sleep(.1)
    else: raise AssertionError('25 windows required')
    time.sleep(1)
    for cycle in range(3):
        call('overview'); time.sleep(.8)
        state = call('dump')
        (out/f'overview-{cycle}.txt').write_text(state)
        card = re.search(r' card=(#[0-9a-f]+)', call('dumpTheme'))[1]
        points = []
        for row in re.findall(r'^p\d+ .* ICON.*$', state, re.M):
            w,h = map(float,re.search(r' drawn ([-\d.]+)x([-\d.]+)',row).groups())
            x,y = map(float,re.search(r' drawnpos ([-\d.]+),([-\d.]+)',row).groups())
            if w > 45 and h > 45 and 0 <= x and 0 <= y and x+w <= 1280 and y+h <= 800:
                points.append(dict(x=x+w*.25,y=y+h*.25,color=card))
        check(bool(points), f'overview has readable icon tiles ({cycle})')
        call('configure',json.dumps(points),probe=True)
        subprocess.run(['spectacle','-b','-f','-n','-o',str(out/f'overview-{cycle}.png')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        stats=json.loads(call('stats',probe=True))
        expected=tuple(int(card[i:i+2],16) for i in (1,3,5))
        check(all(p['samples'] and max(abs(a-b) for a,b in zip(p['last'],expected)) < 8 for p in stats['points']),
              f'real frames show icon cards after returning ({cycle})')
        call('configure','[]',probe=True)
        call('focusIndex',24); time.sleep(.7)
        if cycle == 1:
            call('toggleDock',0); time.sleep(.4)
            call('toggleDock',0); time.sleep(.4)
    check(len(re.findall(r'^p\d+ ',call('dump'),re.M))==25,'navigation and delegate resets preserve all windows')
finally:
    client.terminate(); client.wait(timeout=10)
PY
