#!/usr/bin/env bash
# NEST_OUTPUTS=2. Hyprland presentation profile, real Qt clients, no resize glides.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL Hyprland scenario exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
c++ -fPIC "$KRELLIS_ROOT/scenarios/lifecycle-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
python3 - "$NEST_WORK/client" "$NEST_OUT" <<'PY'
import pathlib, re, subprocess, sys, time

client, out = sys.argv[1], pathlib.Path(sys.argv[2])
clients = []
serial = 0
number = r'(-?[\d.]+)'
rect = number + ',' + number + ' ' + number + 'x' + number

def call(*args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + args[0], *map(str, args[1:])], text=True)

def dump():
    global serial
    value = call('dump')
    (out / f'state-{serial:03}.txt').write_text(value)
    serial += 1
    return value

def row(state, name):
    return next((line for line in state.splitlines() if f"'{name}' screen" in line), '')

def values(line, tag):
    match = re.search(tag + ' ' + rect, line)
    assert match, (tag, line)
    return tuple(map(float, match.groups()))

def scalar(line, tag):
    match = re.search(tag + ' ' + number, line)
    assert match, (tag, line)
    return float(match[1])

def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    assert ok, label

def spawn(name):
    p = subprocess.Popen([client, name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    clients.append(p)
    return p

def samples(name, seconds=.65):
    states = []
    until = time.monotonic() + 3
    while time.monotonic() < until:
        state = dump()
        line = row(state, name)
        if line:
            if not states: until = time.monotonic() + seconds
            states.append((state, line))
        time.sleep(.015)
    return states

def setting(key, value):
    subprocess.check_call(['kwriteconfig6', '--file', 'krellisrc', '--group', 'General', '--key', key, value])
    call('reloadSettings')

try:
    a = spawn('hypr-alpha')
    opening = samples('hypr-alpha')
    check(any('ANIMATION profile hyprland style popin glide 0' in s for s, _ in opening), 'profile works with glide and landMs off')
    animated = [line for _, line in opening if 'animationMs 410' in line]
    check(bool(animated), 'opening uses the 410 ms geometry clock')
    check(any(0 < scalar(line, 'opacity') < 1 for line in animated), 'opening fades in')
    check(any(scalar(line, 'elapsedMs') > 190 and scalar(line, 'opacity') == 1 for line in animated), 'fade finishes before geometry')
    check(all(abs(values(line, 'from')[2] / values(line, 'screen')[2] - .87) < .001 for line in animated), 'opening starts at 87 percent')
    check('FLY' not in opening[-1][1], 'opening releases its animation at rest')

    b = spawn('hypr-beta')
    reflow = samples('hypr-alpha')
    moves = [line for _, line in reflow if 'animationMs 479' in line]
    check(bool(moves), 'new tile reflows its neighbour with the 479 ms clock')
    requests = [scalar(s, 'NATIVE-REQUESTS') for s, line in reflow
                if 'animationMs 479' in line and scalar(line, 'elapsedMs') > 100]
    check(len(requests) > 3 and max(requests) == min(requests), 'paint animation adds no native resize requests')

    call('toggleMaximize', 0)
    time.sleep(.065)
    before = row(dump(), 'hypr-alpha')
    call('toggleMaximize', 0)
    after = row(dump(), 'hypr-alpha')
    check('animationMs 479' in after and scalar(after, 'elapsedMs') < 100, 'maximize reversal restarts the clock')
    check(max(abs(x-y) for x,y in zip(values(before, 'FLY'), values(after, 'from'))) < 130,
          'reversal captures the current painted rectangle')
    time.sleep(.65)
    baseline = {name: values(row(dump(), name), 'screen') for name in ('hypr-alpha', 'hypr-beta')}
    check(call('simNativeResizeStart', 0, 1, .5).strip() == 'true', 'native resize starts with the profile active')
    call('simNativeResizeMove', 60, 0)
    time.sleep(.06)
    resizing = dump()
    check('RESIZE ' in resizing and 'FLY' not in row(resizing, 'hypr-alpha'), 'native resize follows the pointer without a flight')
    call('simNativeResizeEnd', 'false')
    time.sleep(.65)
    check(values(row(dump(), 'hypr-alpha'), 'screen') == baseline['hypr-alpha'], 'native resize cancel restores layout')

    a_rect, b_rect = baseline['hypr-alpha'], baseline['hypr-beta']
    call('simDragStart', 0, a_rect[0] + a_rect[2]/2, 300)
    call('simDragMove', b_rect[0] + b_rect[2]*.3, 400)
    time.sleep(.15)
    preview = dump()
    check('DRAG lifted' in preview, 'drag preview works with the profile')
    time.sleep(.1)
    check(scalar(dump(), 'NATIVE-REQUESTS') == scalar(preview, 'NATIVE-REQUESTS'), 'static drag preview does not repeatedly resize clients')
    call('simDragEnd', 'false')
    cancelled = row(dump(), 'hypr-alpha')
    check('animationMs 479' in cancelled, 'drag cancellation animates back from the lifted card')
    time.sleep(.65)
    check(values(row(dump(), 'hypr-alpha'), 'screen') == baseline['hypr-alpha'], 'drag cancellation restores the original layout')

    # Hide/restore retains native minimize refs, then settles without repeated configures.
    call('toggleHide', 0)
    time.sleep(.05)
    call('toggleHide', 0)
    restore = row(dump(), 'hypr-alpha')
    check('animationMs 479' in restore and 0 < scalar(restore, 'baseOpacity') < 1, 'hide reversal retains fractional opacity')
    time.sleep(.65)
    call('moveToOutput', 0, 1)
    call('selectOutput', 1)
    time.sleep(.65)
    check(bool(row(dump(), 'hypr-alpha')), 'animation survives transfer to the second output')

    # Change only the open/close style; both clocks stay fixed.
    for style in ('slide', 'gnomed'):
        setting('windowAnimation', style)
        name = 'hypr-' + style
        p = spawn(name)
        opening = samples(name)
        first = next(line for _, line in opening if 'animationMs 410' in line)
        source, target = values(first, 'from'), values(first, 'screen')
        if style == 'slide':
            check(source[2:] == target[2:] and source[:2] != target[:2], 'slide preserves size and offsets position')
        else:
            check(source[3] == 0 and abs(source[2] - target[2]) < 1, 'vertical collapse opens from zero height')
        p.terminate()
        closing = []
        until = time.monotonic() + .23
        while time.monotonic() < until:
            state = dump()
            closing.extend(line for line in state.splitlines() if line.startswith('CLOSING ' + name + ' '))
            time.sleep(.01)
        check(bool(closing) and all(0 <= scalar(line, 'opacity') <= 1 for line in closing), style + ' closes through retained window refs')
        if style == 'gnomed':
            check(any(values(line, 'drawn')[3] < values(line, 'from')[3] - 1 for line in closing), 'closing vertical collapse reduces height')
        else:
            check(any(values(line, 'drawn')[:2] != values(line, 'from')[:2] for line in closing), 'closing slide changes position')
        check('CLOSING ' + name not in dump(), style + ' releases its closing reference')

    setting('windowAnimation', 'popin')
    p = spawn('hypr-early')
    until = time.monotonic() + 2
    while time.monotonic() < until:
        state = dump()
        early = row(state, 'hypr-early')
        if early and 'animationMs 410' in early and scalar(early, 'opacity') > 0: break
        time.sleep(.01)
    check(bool(early) and 'animationMs 410' in early, 'early close fixture is genuinely opening')
    time.sleep(.04)
    state = dump()
    index = next(i for i, line in enumerate(line for line in state.splitlines() if ' screen ' in line)
                 if "'hypr-early'" in line)
    call('toggleMaximize', index)
    rebased = row(dump(), 'hypr-early')
    check(scalar(rebased, 'fadeElapsedMs') > scalar(rebased, 'elapsedMs') + 20, 'geometry retarget preserves the original fade clock')
    p.terminate()
    early_closing = []
    until = time.monotonic() + .22
    while time.monotonic() < until:
        early_closing.extend(line for line in dump().splitlines() if line.startswith('CLOSING hypr-early '))
        time.sleep(.01)
    check(bool(early_closing) and 0 < scalar(early_closing[0], 'baseOpacity') < 1, 'closing during opening preserves fractional opacity')

    # Switching profiles live must not strand a minimized client or an animation.
    setting('animationProfile', 'trellis')
    time.sleep(.6)
    check('ANIMATION profile trellis' in dump(), 'profile can be switched live')
    check('FLY' not in row(dump(), 'hypr-alpha'), 'profile switch leaves no running flight')
finally:
    for p in clients:
        if p.poll() is None: p.terminate()
    for p in clients:
        p.wait(timeout=5)
PY
