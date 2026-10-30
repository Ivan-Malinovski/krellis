import os
from pathlib import Path
import re
import subprocess
import time

out = Path(os.environ['NEST_OUT'])

def call(method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + method, *map(str, args)], text=True)

def state():
    return call('dumpOutput', 0)

def count(text):
    return int(re.search(r'NATIVE-REQUESTS (\d+)', text)[1])

def rect(line, key):
    return tuple(map(float, re.search(key + r' ([-\d.]+),([-\d.]+) ([-\d.]+)x([-\d.]+)', line).groups()))

def rows(text):
    return [line for line in text.splitlines() if re.match(r'^p\d+ ', line)]

def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    assert ok, label

def settled():
    for _ in range(100):
        text = state()
        if 'CAMERA-RESIZE pending 0' in text and not any(' FLY ' in line for line in rows(text)):
            time.sleep(.15)  # allow final configure commits
            return state()
        time.sleep(.03)
    raise AssertionError('navigation did not settle')

with (out / 'client.txt').open('w') as log:
    pointer = subprocess.Popen([os.environ['NEST_WORK'] + '/pointer-driver'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert pointer.stdout.readline().strip() == 'READY'
    def event(command):
        pointer.stdin.write(command + '\n')
        pointer.stdin.flush()
        assert pointer.stdout.readline().strip() == 'OK', command
    client = subprocess.Popen([os.environ['NEST_WORK'] + '/client', '25'], stdout=log, stderr=log)
    try:
        call('selectOutput', 0)
        for _ in range(200):
            if len(rows(state())) == 25:
                break
            time.sleep(.1)
        else:
            raise AssertionError('25 managed windows required')
        time.sleep(1)
        before = state()
        call('focusIndex', 24)
        time.sleep(.12)
        during = state()
        check(0 < count(during)-count(before) < 25, 'deep zoom configures visible clients at the start')
        after = settled()
        (out / 'focused.txt').write_text(after)
        visible = []
        for line in rows(after):
            x, y, w, h = rect(line, 'screen')
            if w > 0 and h > 0 and x < 1280 and y < 800 and x+w > 0 and y+h > 0:
                visible.append(line)
        requests = count(after) - count(before)
        check(0 < requests <= len(visible) < 25, f'only visible tiles resize ({requests}, visible {len(visible)})')
        old = {re.search(r"'([^']+)'", line)[1]: rect(line, 'frame') for line in rows(before)}
        for line in rows(after):
            if line not in visible:
                assert rect(line, 'frame') == old[re.search(r"'([^']+)'", line)[1]], line
        check(True, 'offscreen native geometry is retained')
        time.sleep(.2)
        check(count(state()) == count(after), 'settled navigation sends no more requests')
        check(not rows(call('dumpOutput', 1)), 'camera moves never transfer windows to neighbouring output')
        before_noop = count(state())
        call('simPinch', 1.0, 640, 400)
        call('simPinchEnd')
        time.sleep(.1)
        check(count(state()) == before_noop, 'no-op pinch release does not resize offscreen windows')

        # A real client click follows the current paint transform after deferred resizing.
        target = next(line for line in rows(after) if "'zoom-load-24'" in line)
        fx, fy, fw, fh = rect(target, 'frame')
        sx, sy, sw, sh = rect(target, 'screen')
        check(abs(fw-sw) <= 1 and abs(fh-sh) <= 1,
              'focused client lands its destination size')
        bx, by, bw, bh = rect(target, 'client')
        dw, dh = map(float, re.search(r' drawn ([\d.]+)x([\d.]+)', target).groups())
        dx, dy = map(float, re.search(r' drawnpos ([-\d.]+),([-\d.]+)', target).groups())
        u, v = bw / 2, bh / 2
        x, y = dx + (bx+u-fx)*dw/fw, dy + (by+v-fy)*dh/fh
        event(f'move {x} {y}')
        time.sleep(.05)
        event('down')
        event('up')
        time.sleep(.2)
        delivered = (out / 'client.txt').read_text()
        for event_kind in ('CLICK', 'RELEASE'):
            matches = re.findall(rf'^{event_kind} 24 ([\d.]+) ([\d.]+)$', delivered, re.M)
            check(bool(matches) and abs(float(matches[-1][0])-u) < 1 and abs(float(matches[-1][1])-v) < 1,
                  f'{event_kind.lower()} maps into the resized client')

        call('overview')
        settled()
        before = state()
        for i in range(21):
            call('simPinch', 1+i*.05, 640, 400)
            time.sleep(.02)
        time.sleep(.6)  # still holding the gesture: quiet input must not finish it
        during = state()
        check(count(before) == count(during), 'continuous zoom and held gesture send zero client requests')
        check('CAMERA-RESIZE pending 1' in during, 'held gesture retains deferred sizing')
        clicks = len(re.findall(r'^CLICK ', (out / 'client.txt').read_text(), re.M))
        event('move 640 400')
        event('down')
        event('up')
        time.sleep(.05)
        check(len(re.findall(r'^CLICK ', (out / 'client.txt').read_text(), re.M)) == clicks,
              'held navigation gesture blocks client clicks')
        call('simPinchEnd')
        after = settled()
        check(count(after)-count(before) <= 25, 'gesture release coalesces native requests into one batch')

        call('overview')
        settled()
        before = state()
        for i in range(3):
            call('simAxis', -15, 1200, 700, 0x10000000)
            time.sleep(.2)
        during = state()
        check(0 < count(during)-count(before) < 3*25, 'Meta+wheel steps configure visible clients only')
        settled()

        call('overview')
        settled()
        before = state()
        call('simButton', 900, 500, 'true', 0x10000000)
        call('simMotion', 1270, 780)
        call('simButton', 1270, 780, 'false', 0x10000000)
        during = state()
        check(0 < count(during)-count(before) < 25 and 'CAMERA-RESIZE pending 0' in during,
              'Meta+rectangle zoom configures its visible destination immediately')
        settled()

        # A layout edit must break out of deferral and size its clients normally.
        call('focusIndex', 0)
        call('simPinch', 1.1, 640, 400)
        time.sleep(.06)
        client.terminate()
        client.wait(timeout=10)
        time.sleep(.8)
        check(not rows(state()) and 'CAMERA-RESIZE pending 0' in state(), 'close during deferred navigation cancels the pending batch')
        check(not rows(call('dumpOutput', 1)), 'no delayed cross-output ownership changes')
        call('simPinchEnd')
    finally:
        pointer.terminate()
        pointer.wait(timeout=5)
        if client.poll() is None:
            client.terminate()
            client.wait(timeout=10)
