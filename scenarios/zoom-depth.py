"""Exercise successive zoom levels, blur recovery and GPU allocation errors; report timings without FPS thresholds."""
import json
import os
from pathlib import Path
import re
import subprocess
import time

out = Path(os.environ['NEST_OUT'])
width, height = map(int, os.environ.get('ZOOM_DEPTH_SIZE', '1280 800').split())
blur_enabled = os.environ.get('ZOOM_DEPTH_BLUR', '1') == '1'

def db(path, method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', path, method, *map(str, args)], text=True).strip()

def call(method, *args):
    return db('/Krellis', 'org.kde.kwin.KWin.KrellisEffect.' + method, *args)

def probe(method, *args):
    return db('/KrellisPaintProbe', 'org.kde.kwin.KrellisPaintProbe.' + method, *args)

def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    assert ok, label

def rows(state):
    return re.findall(r'^p\d+ .*$', state, re.M)

def visible(state):
    for row in rows(state):
        match = re.search(r' drawn ([\d.]+)x([\d.]+) drawnpos ([-\d.]+),([-\d.]+)', row)
        if match:
            w, h, x, y = map(float, match.groups())
            if x < width and y < height and x+w > 0 and y+h > 0:
                yield row, w, h

def percentile(values, fraction):
    return round(sorted(values)[int((len(values)-1)*fraction)], 3) if values else None

client = subprocess.Popen([os.environ['NEST_WORK'] + '/client', '25'])
try:
    if blur_enabled:
        check(db('/Effects', 'org.kde.kwin.Effects.loadEffect', 'blur') == 'true', 'real KWin blur effect loads')
    else:
        loaded = db('/Effects', 'org.freedesktop.DBus.Properties.Get', 'org.kde.kwin.Effects', 'loadedEffects').splitlines()
        check('blur' not in loaded, 'KWin blur is disabled for the entire depth sweep')
    (out / 'workload.json').write_text(json.dumps(dict(width=width, height=height,
        blurEffect=blur_enabled, opacity=os.environ.get('ZOOM_LOAD_OPACITY', '0.8'),
        blurRadius=os.environ.get('ZOOM_LOAD_BLUR_RADIUS', '16')), indent=2))
    for _ in range(240):
        if len(rows(call('dump'))) == 25:
            break
        time.sleep(.1)
    else:
        raise AssertionError('25 Kitty clients required')
    time.sleep(1)
    call('overview')
    time.sleep(.7)
    results = []
    cameras = set()
    for direction in (-15, 15):
        for step in range(13):
            before = call('dump')
            probe('timing', 'true')
            call('simAxis', direction, width-80, height-100, 0x10000000)
            time.sleep(.12)
            during = call('dump')
            changing = [row for row, _, _ in visible(during) if ' FLY ' in row]
            if during.splitlines()[0] != before.splitlines()[0]:
                check(bool(changing) and all(' blur 0 ' in row for row in changing),
                      f'camera motion suspends blur ({direction}, {step})')
            else:
                check(not changing, f'zoom limit leaves settled presentation intact ({direction}, {step})')
            time.sleep(.53)
            probe('timing', 'false')
            data = json.loads(probe('timingJson'))
            state = call('dump')
            cameras.add(state.splitlines()[0])
            check(len(rows(state)) == 25, f'window ownership survives zoom ({direction}, {step})')
            normal = [row for row, w, h in visible(state) if 32 <= w <= width*1.25 and 32 <= h <= height*1.25]
            check(bool(normal) and all(' blur 1 ' in row and ' FLY ' not in row for row in normal),
                  f'visible normal tiles regain blur ({direction}, {step})')
            oversized = [row for row, w, h in visible(state) if w > width*1.25 or h > height*1.25]
            check(all(' blur 0 ' in row for row in oversized), f'oversized visible tiles keep bounded blur ({direction}, {step})')
            requests = lambda text: int(re.search(r'NATIVE-REQUESTS (\d+)', text)[1])
            result = dict(direction=direction, step=step, camera=state.splitlines()[0],
                          frames=len(data['cpuMs']), nativeRequests=requests(state)-requests(before),
                          cpuP95=percentile(data['cpuMs'], .95), intervalP95=percentile(data['intervalMs'], .95),
                          cpuMax=max(data['cpuMs'], default=0),
                          gpuP95=percentile([r['gpuMs'] for r in data.get('gpu', [])], .95),
                          gpuMax=max([r['gpuMs'] for r in data.get('gpu', [])], default=0),
                          drawsMax=max([r['draws'] for r in data.get('drawStats', [])], default=0),
                          shadowOnlyMax=max([r['shadowOnly'] for r in data.get('drawStats', [])], default=0),
                          earlyP95=percentile([v for t,v in zip(data.get('startMs', []),data['cpuMs']) if t < 350], .95),
                          lateP95=percentile([v for t,v in zip(data.get('startMs', []),data['cpuMs']) if t >= 350], .95))
            results.append(result)
            (out / f'phase-{direction}-{step}.json').write_text(json.dumps(data))
            (out / f'phase-{direction}-{step}.txt').write_text(state)
            (out / 'depth.json').write_text(json.dumps(results, indent=2))
    check(len(cameras) >= 10, 'workload traverses at least ten distinct zoom destinations')
    call('simPinch', 1.1, width/2, height/2)
    time.sleep(.65)
    check(all(' blur 0 ' in row for row, _, _ in visible(call('dump'))), 'quiet held gesture keeps blur suspended')
    call('simPinchEnd')
    time.sleep(.8)
    check(any(' blur 1 ' in row for row, _, _ in visible(call('dump'))), 'gesture release restores visible blur')
    log = (out / 'kwin.log').read_text()
    check('Failed to create an offscreen framebuffer' not in log and 'GL_INVALID_VALUE' not in log,
          'deep blurred navigation has no invalid GPU allocations')
finally:
    client.terminate()
    client.wait(timeout=10)
