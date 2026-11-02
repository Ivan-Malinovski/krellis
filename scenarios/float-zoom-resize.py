import json
import os
import re
import subprocess
import time
from pathlib import Path

DX = 60  # pointer travel per phase, in logical output pixels


def call(method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + method, *map(str, args)], text=True).strip()


def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    assert ok, label


def rows(text):
    return [line for line in text.splitlines() if re.match(r'^p\d+ ', line)]


def row(text, panel):
    return next(line for line in rows(text) if line.split()[0] == panel)


def rect(text, key):
    return tuple(map(float, re.search(key + r' ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)', text).groups()))


def area(text):
    return rect(next(line for line in text.splitlines() if line.startswith('area ')), 'area')


def camera(text):
    return tuple(map(float, re.match(r'camera ([-\d.]+) ([-\d.]+) ([\d.]+) ([\d.]+)', text).groups()))


def normalised():
    """The stored canvas-normalised floating rects, in document order."""
    assert call('saveSession') == 'true'
    doc = json.loads((Path(os.environ['XDG_STATE_HOME']) / 'krellis/session.json').read_text())
    return [tuple(entry['rect']) for entry in doc['canvases'][0]['floating']]


def settled():
    """Wait until the canvas is quiet: no flight, no deferred camera resize, stable rows."""
    last = None
    for _ in range(200):
        text = call('dump')
        if 'CAMERA-RESIZE pending 0' in text and ' FLY ' not in text and len(rows(text)) == 4:
            key = (tuple(rows(text)), text.splitlines()[0],
                   [line for line in text.splitlines() if line.startswith('area ')])
            if key == last:
                return text
            last = key
        else:
            last = None
        time.sleep(.1)
    raise AssertionError('layout did not settle')


clients = []
try:
    for name in 'ABCD':
        clients.append(subprocess.Popen(['alacritty', '--title', f'float-zoom-{name}', '-e', 'sleep', '600'],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        time.sleep(1.2)
    settled()
    call('overview')
    state = settled()
    check(len(rows(state)) == 4, 'four tiles')
    # Two floats; the first one under test, the second one must not move.
    call('toggleFloat', 2)
    time.sleep(.6)
    call('toggleFloat', 3)
    state = settled()
    floats = [line for line in rows(state) if ' FLOAT' in line]
    check(len(floats) == 2, 'two floats')
    target, neighbour = 'p3', 'p4'
    check(f"'float-zoom-C'" in row(state, target) and f"'float-zoom-D'" in row(state, neighbour),
          'the floats are the two windows under test')
    index = rows(state).index(row(state, target))
    # The seed's minWidth/minHeight rule makes the native frame of the float far bigger than the tile it
    # is painted into: the "already scaled window" effect/native_resize.cpp warns about, painted != native.
    painted, native = rect(row(state, target), 'screen'), rect(row(state, target), 'frame')
    check(painted[2] / native[2] < 0.6 and painted[3] / native[3] < 0.6,
          f'the float is painted at {painted[2]:.0f}x{painted[3]:.0f} inside a {native[2]:.0f}x{native[3]:.0f} native frame')
    print(f'stored floats {normalised()}', flush=True)
    print(f'painted {painted} neighbour {rect(row(state, neighbour), "screen")}', flush=True)

    def phase(label, state, zoom):
        """One native right-edge resize of the float at whatever camera the caller set up."""
        cam, box = camera(state), area(state)
        check(abs(cam[2] - zoom[0]) < 0.02 and abs(cam[3] - zoom[1]) < 0.02,
              f'{label}: camera spans {zoom[0]:.3f}x{zoom[1]:.3f} (got {cam[2]:.3f}x{cam[3]:.3f})')
        b = rect(row(state, target), 'screen')
        other = rect(row(state, neighbour), 'screen')
        norm = normalised()

        def preview():
            check(call('simNativeResizeStart', index, 1, 0.5) == 'true', f'{label}: grab the float right edge natively')
            check(call('simNativeResizeMove', DX, 0) == 'true', f'{label}: move the pointer {DX}px right')
            time.sleep(.15)
            during = call('dump')
            line = row(during, target)
            print(line, flush=True)
            check(call('saveSession') == 'false', f'{label}: a live preview is never saved')
            return line, rect(line, 'screen'), rect(row(during, neighbour), 'screen')

        line, a, neighbour_during = preview()
        check(abs((a[0] + a[2]) - (b[0] + b[2]) - DX) <= 1,
              f'{label}: painted right edge follows the pointer ({a[0] + a[2] - b[0] - b[2]:.2f} for {DX})')
        check(abs(a[0] - b[0]) <= 1, f'{label}: painted left edge stays put ({a[0] - b[0]:.2f})')
        check(abs(a[2] - b[2] - DX) <= 1, f'{label}: painted width grows by dx ({a[2] - b[2]:.2f})')
        drawn = tuple(map(float, re.search(r' drawn ([\d.]+)x([\d.]+)', line).groups()))
        check(abs(drawn[0] - a[2]) <= 1 and abs(drawn[1] - a[3]) <= 1,
              f'{label}: drawn size matches the painted float ({drawn[0]:.1f}x{drawn[1]:.1f} vs {a[2]:.1f}x{a[3]:.1f})')
        check(neighbour_during == other, f'{label}: the second float does not move')
        check(a[0] >= -0.5 and a[0] + a[2] <= box[2] + 0.5 and a[1] >= -0.5 and a[1] + a[3] <= box[3] + 0.5,
              f'{label}: the preview stays inside the output, so nothing clamped')
        check(call('simNativeResizeEnd', 'false') == 'true', f'{label}: cancel the preview')
        check(rect(row(settled(), target), 'screen') == b, f'{label}: a cancelled preview restores the float')
        check(normalised() == norm, f'{label}: a cancelled preview restores the stored rect')

        # Only a commit writes the model, so repeat the same drag and commit it. The stored float is the
        # painted rect divided by the output: exactly one of the two floats grows by dx/output there.
        preview()
        check(call('simNativeResizeEnd', 'true') == 'true', f'{label}: commit the preview')
        after = settled()
        stored = normalised()
        committed = rect(row(after, target), 'screen')
        grown = [i for i in (0, 1) if abs((stored[i][2] - norm[i][2]) - DX / box[2]) < 0.002]
        check(abs(committed[2] - b[2] - DX) <= 1, f'{label}: the committed float is dx wider ({committed[2] - b[2]:.2f})')
        check(len(grown) == 1, f'{label}: exactly one stored float grows by dx/output ({[round(stored[i][2] - norm[i][2], 4) for i in (0, 1)]})')
        if grown:
            i = grown[0]
            check(abs(stored[i][0] - norm[i][0]) < 0.002,
                  f'{label}: stored float origin does not move ({stored[i][0] - norm[i][0]:.4f})')
            check(stored[1 - i] == norm[1 - i], f'{label}: the stored second float does not move')
        return after

    # Camera 1.0: the control case.
    state = phase('zoom 1.0', settled(), (1.0, 1.0))

    # A durable zoom onto one of the two docked tiles (camera 0.5 x 1.0). Floats are screen-space, so the
    # camera must not move or rescale them: only the pointer mapping is under test.
    before = rect(row(state, target), 'screen')
    call('focusIndex', 0)
    state = settled()
    zoom = camera(state)
    check(zoom[2] < 0.7 and zoom[3] >= 0.98, f'focus zoomed the camera in on the docked tile ({zoom[2]:.3f}x{zoom[3]:.3f})')
    check(rect(row(state, target), 'screen') == before, 'the overlay float does not follow the camera zoom')
    state = phase(f'zoomed in {1 / zoom[2]:.2f}', state, (zoom[2], zoom[3]))

    # A camera 2x the canvas needs a pinch, and a held pinch does not reconfigure clients: floats live in
    # view space, so they are unaffected either way. End the gesture afterwards (it reframes a docked tile).
    call('overview')
    state = settled()
    before = rect(row(state, target), 'screen')
    call('simPinch', 0.5, 640, 400)
    time.sleep(.5)
    state = settled()
    zoom = camera(state)
    check(zoom[2] > 1.4 and abs(zoom[2] - zoom[3]) < 0.02,
          f'the pinch stretched the camera to {zoom[2]:.3f}x{zoom[3]:.3f}')
    check(rect(row(state, target), 'screen') == before, 'the overlay float does not follow the pinch zoom')
    phase(f'zoomed out {1 / zoom[2]:.2f}', state, (zoom[2], zoom[3]))
    call('simPinchEnd')
    time.sleep(.5)
    call('overview')
    state = settled()
    check(camera(state)[2:] == (1.0, 1.0), 'the camera is back at 1.0x1.0')
finally:
    for client in clients:
        client.terminate()
    for client in clients:
        try:
            client.wait(timeout=5)
        except subprocess.TimeoutExpired:
            client.kill()
