#!/usr/bin/env bash
# Visual feedback uses real QML scenes; screenshots are for manual inspection.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
for n in A B C D; do
    alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 &
    sleep 1.5
done
sleep 1
python3 - <<'PY'
import os, re, subprocess, time

def call(method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + method, *map(str, args)], text=True)
def dump():
    result = call('dump')
    print(result, flush=True)
    return result

def check(ok, label):
    print(('PASS ' if ok else 'FAIL ') + label, flush=True)
    if not ok: raise SystemExit(1)

def shot(name):
    subprocess.run([os.environ['KRELLIS_ROOT'] + '/tools/shot', name], check=True)

def rects(text):
    return {name: tuple(map(float, values)) for name, *values in re.findall(
        r"^p\d+ 'win-([ABCD])' screen ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)", text, re.M)}

feedback_only = os.environ.get('EXPECT_FEEDBACK_ONLY') == '1'
def requests(text):
    return int(re.search(r'^NATIVE-REQUESTS (\d+)', text, re.M).group(1))

initial = dump()
check(re.search(r'MOTION .*scene 1 labels [1-9]', initial), 'overview labels rendered by QML')
shot('polish-overview')
call('focusIndex', 0)
time.sleep(1.8)
check('labels 0' in dump(), 'labels disappear when contents are large')
call('overview')
time.sleep(1.8)
a, b, c, d = (rects(dump())[n] for n in 'ABCD')
call('simDragStart', 0, round(a[0] + a[2]/2), 300)
call('simDragMove', round(d[0] + d[2]*.35), 400)
if feedback_only:
    time.sleep(.19)
    early = dump()
    time.sleep(.08)
    later = dump()
    first = re.search(r'^HIGHLIGHT .* ([\d.]+)x([\d.]+)', early, re.M)
    second = re.search(r'^HIGHLIGHT .* ([\d.]+)x([\d.]+)', later, re.M)
    check(first and second and first.groups() != second.groups(), 'drop highlight animates with glides off')
    check(requests(early) == requests(later), 'highlight animation makes no native resize requests')
    time.sleep(.38)
else:
    time.sleep(.65)
check(re.search(r'MOTION lift (?:0\.[8-9]\d|1\.00)', dump()), 'drag lifts the window')
shot('polish-lift')
call('simDragEnd', 'true')
time.sleep(.04)
released = dump()
check(re.search(r'MOTION .*landing p\d+', released), 'release starts landing feedback')
if feedback_only:
    time.sleep(.08)
    check(requests(dump()) == requests(released), 'landing feedback makes no native resize requests')
time.sleep(1.2)
check('busy 0' in dump(), 'landing feedback becomes idle')
call('undo')
time.sleep(.05)
undone = dump()
check(re.search(r'MOTION .*rewind [1-9]', undone), 'undo highlights affected windows')
if feedback_only:
    time.sleep(.08)
    check(requests(dump()) == requests(undone), 'undo feedback makes no native resize requests')
time.sleep(1.5)
check(rects(dump()) == rects(initial), 'undo returns to original layout')
check('busy 0' in dump(), 'undo feedback becomes idle')
call('simDragStart', 0, round(a[0] + a[2]/2), 300)
call('simDragMove', round(c[0] + c[2]*.35), 400)
time.sleep(.25)
call('simDragEnd', 'false')
time.sleep(1.2)
check(rects(dump()) == rects(initial), 'cancel preserves the original layout')
call('focusIndex', 1)
time.sleep(1.5)
call('saveView')
time.sleep(.3)
call('simText', 'Reading')
call('simKey', 16777220, 'true')
call('simKey', 16777220, 'false')
time.sleep(.4)
call('overview')
time.sleep(1.5)
check('frames 1' in dump(), 'overview shows the saved view boundary')
shot('polish-saved-view')
call('toggleFloatActive')
time.sleep(1.4)
floating = dump()
check(' FLOAT' in floating, 'window floats for undo coverage')
call('undo')
time.sleep(.05)
if not feedback_only: check(' FLY ' in dump(), 'float-to-tile undo starts from its painted rectangle')
time.sleep(1.5)
check(rects(dump()) == rects(initial), 'undo of floating restores the tiles')
call('redo')
time.sleep(.05)
if not feedback_only: check(' FLY ' in dump(), 'tile-to-float redo animates')
time.sleep(1.5)
check(rects(dump()) == rects(floating), 'redo restores the floating rectangle')
log = open(os.environ['NEST_OUT'] + '/kwin.log').read()
check('motion_overlay.qml' not in log, 'overlay has no QML errors')
PY
rc=$?
pkill -P "$$" -x alacritty
exit "$rc"
