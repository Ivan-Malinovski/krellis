import os
import re
import subprocess
import time

PIXEL = (1280, 800)   # every nested output is 1280x800 pixels; the scales make that logical size differ
SCALES = (1.25, 1.5)
BAR = 48


def call(method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + method, *map(str, args)], text=True).strip()


def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    assert ok, label


def rows(text):
    return [line for line in text.splitlines() if re.match(r'^p\d+ ', line)]


def rect(text, key):
    return tuple(map(float, re.search(key + r' ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)', text).groups()))


def area(text):
    return rect(next(line for line in text.splitlines() if line.startswith('area ')), 'area')


def camera(text):
    return tuple(map(float, re.match(r'camera ([-\d.]+) ([-\d.]+) ([\d.]+) ([\d.]+)', text).groups()))


def bar(text):
    return rect(next(line for line in text.splitlines() if line.startswith('area ')), 'bar')


def output(i):
    return call('dumpOutput', i)


def tile(text, caption):
    return next(line for line in rows(text) if caption in line)


def settled(outputs=2):
    """Both canvases quiet: stable rows and areas over two consecutive dumps."""
    last = None
    for _ in range(200):
        text = call('dump')
        if 'CAMERA-RESIZE pending 0' in text and ' FLY ' not in text:
            key = (tuple(rows(text)), tuple(line for line in text.splitlines() if line.startswith('area ')))
            if key == last and sum(1 for line in text.splitlines() if line.startswith('OUTPUT ')) == outputs:
                return text
            last = key
        else:
            last = None
        time.sleep(.1)
    raise AssertionError('the canvases did not settle')


def place(caption, dest):
    for source in (0, 1):
        text = output(source)
        for i, line in enumerate(rows(text)):
            if f"'{caption}'" in line:
                call('selectOutput', source)
                call('moveToOutput', i, dest)
                return
    raise AssertionError(f'no window {caption}')


def click(x, y, method='simButton'):
    """Left-click (or a middle-click) a logical point, the way the effect receives it. What the effect
    returns is not a hit test (over a window it also claims the click for the scaled surface), so the
    assertions read the state the click changed instead."""
    if method == 'simMiddleButton':
        call(method, x, y, 'true')
        call(method, x, y, 'false')
    else:
        call(method, x, y, 'true', 0)
        call(method, x, y, 'false', 0)
    time.sleep(.3)


def layout(i):
    """What a canvas looks like: its camera, its area and its painted tiles, without the dump counters."""
    text = output(i)
    return camera(text), area(text), tuple(sorted(rect(line, 'screen') for line in rows(text)))


def naming(i):
    return int(re.search(r'^NAMING (\d+)', output(i), re.M)[1])


clients = []
try:
    for name in 'ABCD':
        clients.append(subprocess.Popen(['alacritty', '--title', f'frac-{name}', '-e', 'sleep', '600'],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        time.sleep(1.2)
    settled()
    check(all(any(line.startswith('area ') for line in output(i).splitlines()) for i in (0, 1)),
          'the nested compositor has a canvas per output')

    for caption, dest in (('frac-A', 0), ('frac-B', 0), ('frac-C', 1), ('frac-D', 1)):
        place(caption, dest)
    call('toggleBar')  # the bar is off by default and only the bar's hit testing is worth clicking here
    for i in (0, 1):
        call('selectOutput', i)
        call('overview')
    state = settled()

    # --- the geometry the effect works in is logical: pixelSize / scale -----------------------------------
    for i, scale in enumerate(SCALES):
        a = area(output(i))
        want = (PIXEL[0] / scale, PIXEL[1] / scale - BAR)
        check(abs(a[2] - want[0]) < 0.5 and abs(a[3] - want[1]) < 0.5,
              f'output {i} at scale {scale}: area is {a[2]:.2f}x{a[3]:.2f}, logical {PIXEL[0] / scale:.2f}x{PIXEL[1] / scale - BAR:.2f}')
        check(a[2] < PIXEL[0] - 100, f'output {i} at scale {scale}: the area is not the device size ({a[2]:.1f} of {PIXEL[0]})')
    check(abs(area(output(1))[2] - round(area(output(1))[2])) > 0.05,
          f'the 1.5 output has a fractional logical width ({area(output(1))[2]:.3f})')

    # --- tiles are painted at the logical size, inside the logical rect ------------------------------------
    for i, scale in enumerate(SCALES):
        text = output(i)
        a = area(text)
        tiles = rows(text)
        check(len(tiles) == 2, f'output {i} at scale {scale}: two tiles ({len(tiles)})')
        left, right = sorted(tiles, key=lambda line: rect(line, 'screen')[0])
        sl, sr = rect(left, 'screen'), rect(right, 'screen')
        for name, r in (('left', sl), ('right', sr)):
            check(r[0] >= a[0] + 5.5 and r[0] + r[2] <= a[0] + a[2] - 5.5 and r[1] >= a[1] + 5.5 and r[1] + r[3] <= a[1] + a[3] - 5.5,
                  f'output {i} at scale {scale}: the {name} tile is painted inside the output rect')
            check(abs(r[2] - (a[2] - 24) / 2) < 0.6 and abs(r[3] - (a[3] - 12)) < 0.6,
                  f'output {i} at scale {scale}: the {name} tile is {r[2]:.2f}x{r[3]:.2f}, the logical half-tile')
            check(abs(r[2] - ((PIXEL[0] - 24) / 2)) > 50,
                  f'output {i} at scale {scale}: the {name} tile is not sized from device pixels')
        check(abs(sl[0] + sl[2] + 12 - sr[0]) < 0.6, f'output {i} at scale {scale}: the layout keeps its 12 px gap')
        for line in tiles:
            painted, frame = rect(line, 'screen'), rect(line, 'frame')
            drawn = tuple(map(float, re.search(r' drawn ([\d.]+)x([\d.]+)', line).groups()))
            buffer = tuple(map(float, re.search(r' buffer ([\d.]+)x([\d.]+)', line).groups()))
            check(abs(drawn[0] - painted[2]) <= 1 and abs(drawn[1] - painted[3]) <= 1,
                  f'output {i} at scale {scale}: {line.split()[0]} draws its painted size ({drawn[0]:.1f}x{drawn[1]:.1f})')
            check(abs(buffer[0] - frame[2]) <= 1 and abs(buffer[1] - frame[3]) <= 1,
                  f'output {i} at scale {scale}: {line.split()[0]} buffers its native frame ({buffer[0]:.1f}x{buffer[1]:.1f})')

    # --- a middle-click on the painted seam flips the split; a doubly scaled click misses it --------------
    for i, scale in enumerate(SCALES):
        other = 1 - i
        text = output(i)
        a = area(text)
        before = sorted((rect(line, 'screen') for line in rows(text)), key=lambda r: r[0])
        seam_x = (before[0][0] + before[0][2] + before[1][0]) / 2
        seam_y = a[1] + a[3] / 2
        untouched = layout(other)
        click(seam_x * scale, seam_y, 'simMiddleButton')
        check(sorted((rect(line, 'screen') for line in rows(output(i))), key=lambda r: r[0]) == before,
              f'output {i} at scale {scale}: a click where the seam would be if scaled twice changed no tile')
        check(layout(other) == untouched, f'output {i} at scale {scale}: the other canvas is untouched')
        click(seam_x, seam_y, 'simMiddleButton')
        flipped = sorted((rect(line, 'screen') for line in rows(output(i))), key=lambda r: r[1])
        check(abs(flipped[0][0] - flipped[1][0]) < 0.6 and flipped[0][1] + flipped[0][3] < flipped[1][1],
              f'output {i} at scale {scale}: the split flipped to two stacked tiles')
        check(abs(flipped[0][2] - (a[2] - 12)) < 0.6 and abs(flipped[0][3] - (a[3] - 24) / 2) < 0.6,
              f'output {i} at scale {scale}: the stacked tiles fill the logical area')
        # Flip it back through the seam the flip just made.
        seam_y2 = (flipped[0][1] + flipped[0][3] + flipped[1][1]) / 2
        click(flipped[0][0] + flipped[0][2] / 2, seam_y2, 'simMiddleButton')
        check(sorted((rect(line, 'screen') for line in rows(output(i))), key=lambda r: r[0]) == before,
              f'output {i} at scale {scale}: the split flipped back to the two columns')

    # --- a click on the bar's save key starts framing, on that output only ---------------------------------
    for i, scale in enumerate(SCALES):
        other = 1 - i
        a, b = area(output(i)), bar(output(i))
        y = b[1] + BAR / 2
        hits = {}
        for x in range(int(b[0]) + 4, int(b[0] + b[2]) - 4, 8):
            hits.setdefault(call('barHit', x, y), x)
        check('save' in hits, f'output {i} at scale {scale}: barHit finds the save key on the bar ({sorted(k for k in hits if k)})')
        if 'save' not in hits:
            continue
        other_before = naming(other)
        click(hits['save'], y)
        check(naming(i) == 1, f'output {i} at scale {scale}: the click started framing on this output')
        check(naming(other) == other_before, f'output {i} at scale {scale}: the other output is not framing')
        call('simKey', 16777216, 'true')  # Escape
        call('simKey', 16777216, 'false')
        time.sleep(.3)
        check(naming(i) == 0, f'output {i} at scale {scale}: Esc cancelled framing')
finally:
    for client in clients:
        client.terminate()
    for client in clients:
        try:
            client.wait(timeout=5)
        except subprocess.TimeoutExpired:
            client.kill()
