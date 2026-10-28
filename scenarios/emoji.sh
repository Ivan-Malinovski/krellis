#!/usr/bin/env bash
# Real KDE Emoji Selector, with a float-only application rule.
set -euo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
trap 'echo "FAIL emoji scenario exited unexpectedly"' ERR
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
command -v plasma-emojier >/dev/null
c++ -fPIC "$KRELLIS_ROOT/scenarios/lifecycle-client.cpp" -o "$NEST_WORK/client" $(pkg-config --cflags --libs Qt6Widgets)
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
try:
    clients.append(subprocess.Popen([client, 'emoji-neighbour'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    time.sleep(.5)
    p, index, line = open_emoji()
    check(' FLOAT' in line, 'real KDE Emoji Selector opens floating')
    time.sleep(.6)
    call('toggleDock', index)
    check(' FLOAT' in emoji()[1], 'float-only rule rejects the dock shortcut')
    match = re.search(r'screen ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)', emoji()[1])
    x, y, w, h = map(float, match.groups())
    call('simDragStart', index, x+w/2, y+14)
    call('simDragMove', 8, 400)
    time.sleep(.2)
    call('simDragEnd', 'true')
    time.sleep(.6)
    check(' FLOAT' in emoji()[1], 'float-only rule rejects an edge docking drag')
    p.terminate(); p.wait(timeout=5)
    time.sleep(.3)
    p, _, line = open_emoji()
    check(' FLOAT' in line, 'reopened selector still floats')
finally:
    for p in clients:
        if p.poll() is None: p.terminate()
    for p in clients: p.wait(timeout=5)
PY
