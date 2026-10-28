#!/usr/bin/env bash
# Real KDE Emoji Selector with floatWidth/floatHeight/floatAtPointer: it opens 420x480 with its corner at the pointer, clamped to the screen.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL emoji scenario exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
command -v plasma-emojier >/dev/null
python3 - "$NEST_WORK/client" "$NEST_OUT" <<'PY'
import pathlib, re, subprocess, sys, time
client, out = sys.argv[1], pathlib.Path(sys.argv[2])
clients = []
def call(method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + method, *map(str, args)], text=True)
def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    assert ok, label
def emoji():
    text = call('dump')
    (out / 'state.txt').write_text(text)
    rows = [line for line in text.splitlines() if ' screen ' in line]
    return next(((i, line) for i, line in enumerate(rows) if "app 'org.kde.plasma.emojier'" in line), (-1, ''))
def open_emoji():
    p = subprocess.Popen(['plasma-emojier'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    clients.append(p)
    until = time.monotonic() + 4
    while time.monotonic() < until:
        index, line = emoji()
        if line: return p, index, line
        time.sleep(.1)
    raise AssertionError('real emoji selector was not tracked')
def move(x, y):
    call('simMotion', x, y)
    time.sleep(.2)
def geometry(line):
    m = re.search(r'screen ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)', line)
    return tuple(map(float, m.groups()))
try:
    move(300, 200)
    p, index, line = open_emoji()
    time.sleep(.8)
    x, y, w, h = geometry(emoji()[1])
    print('geometry', x, y, w, h, flush=True)
    check(' FLOAT' in line, 'selector floats')
    check(abs(w - 420) <= 24 and abs(h - 480) <= 24, 'selector opens at the rule size, not 60% x 65%')
    check(abs(x - 300) <= 24 and abs(y - 200) <= 24, 'selector opens with its corner at the pointer')
    p.terminate(); p.wait(timeout=5)
    time.sleep(.5)
    move(1275, 795)
    p, index, line = open_emoji()
    time.sleep(.8)
    x, y, w, h = geometry(emoji()[1])
    print('geometry', x, y, w, h, flush=True)
    check(x + w <= 1280 + 1 and y + h <= 800 + 1, 'selector near the bottom-right corner stays on screen')
    check(x < 1275 - 100 and y < 795 - 100, 'selector shifted back toward the pointer side')
finally:
    for p in clients:
        if p.poll() is None: p.terminate()
    for p in clients: p.wait(timeout=5)
PY
