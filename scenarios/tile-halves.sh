# Window-body drops split a single tile; gap drops share the row. No physical pointer required.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
for n in A B C; do
    alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 &
    sleep 1.5
done
sleep 1
python3 - <<'PY'
import re
import subprocess
import time

def call(method, *args):
    return subprocess.check_output(['qdbus6', 'org.kde.KWin', '/Krellis',
        'org.kde.kwin.KWin.KrellisEffect.' + method, *map(str, args)], text=True)

def rectangles():
    dump = call('dump')
    print(dump, flush=True)
    return {name: tuple(map(float, values)) for name, *values in re.findall(
        r"^p\d+ 'win-([ABC])' screen ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)", dump, re.M)}

def check(condition, label):
    print(('PASS ' if condition else 'FAIL ') + label, flush=True)
    if not condition:
        raise SystemExit(1)

initial = rectangles()
a, b, c = (initial[n] for n in 'ABC')
gap_width = c[0] - (b[0] + b[2])
# Drop A well inside C's left half: removing A expands B and C to half the output,
# then only C shares its half with A. Repeat for preview and committed geometry.
check('true' in call('simDragStart', 0, round(a[0] + a[2]/2), 300), 'grab A')
call('simDragMove', round(c[0] + c[2]*0.35), 400)
time.sleep(0.8)
preview = rectangles()
check(abs(preview['B'][2] - (2*preview['C'][2] + gap_width)) < 4, 'preview only splits C')
call('simDragEnd', 'true')
time.sleep(1.3)
body = rectangles()
check(abs(body['A'][2] - body['C'][2]) < 3, 'body drop gives A half of C')
check(abs(body['B'][2] - (2*body['C'][2] + gap_width)) < 4, 'B keeps its space after source removal')
call('undo')
time.sleep(1.3)
check('true' in call('simDragStart', 0, round(a[0] + a[2]/2), 300), 'grab A again')
call('simDragMove', round((b[0] + b[2] + c[0])/2), 400)
time.sleep(0.8)
call('simDragEnd', 'true')
time.sleep(1.3)
gap = rectangles()
check(max(r[2] for r in gap.values()) - min(r[2] for r in gap.values()) < 3,
      'gap drop shares space across all three tiles')
PY
rc=$?
pkill -P "$$" -x alacritty
exit "$rc"
