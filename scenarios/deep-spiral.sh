# Twelve windows dropped one after another into alternating sides of the previous one (a spiral). With the default
# tile minimum of 1 px the splits keep their proportions, so the smallest tile gets far below the old 320x200 floor;
# EXPECT=floor (seed scenarios/deep-spiral.rc) shows the opt-in floor: tiles stop shrinking near 320x200 scaled to fit.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
for n in $(seq 1 12); do
  alacritty --title "sp-$n" -e sh -c "echo $n; sleep 600" >/dev/null 2>&1 &
  sleep 1.2
done
sleep 2
EXPECT=${EXPECT:-deep} python3 - <<'PY'
import os, re, subprocess, sys, time
def call(*a):
    return subprocess.run(["qdbus6", "org.kde.KWin", "/Trellis", "org.kde.kwin.KWin.TrellisEffect." + a[0], *map(str, a[1:])],
                          capture_output=True, text=True).stdout
def panels():
    out = []
    for l in call("dump").splitlines():
        m = re.match(r"p\d+ '([^']*)' screen ([\d.\-]+),([\d.\-]+) ([\d.]+)x([\d.]+)", l)
        if m: out.append((m.group(1), *map(float, m.groups()[1:])))
    return out
titles = [p[0] for p in panels()]
target, zones = titles[0], ["right", "bottom", "left", "top"]
for k, t in enumerate(titles[1:]):
    ps = panels(); idx = {p[0]: i for i, p in enumerate(ps)}
    src, tgt = ps[idx[t]], ps[idx[target]]
    sx, sy = src[1] + src[3] / 2, src[2] + src[4] / 2
    x, y, w, h = tgt[1:]
    px, py = {"right": (x + w * .9, y + h / 2), "bottom": (x + w / 2, y + h * .9),
              "left": (x + w * .1, y + h / 2), "top": (x + w / 2, y + h * .1)}[zones[k % 4]]
    call("simDragStart", idx[t], int(sx), int(sy))
    for f in (.25, .5, .75, 1):
        call("simDragMove", int(sx + (px - sx) * f), int(sy + (py - sy) * f)); time.sleep(.06)
    time.sleep(.4); call("simDragEnd", "true"); time.sleep(1.4)
    target = t
ps = panels()
for p in ps: print("%-6s %6.0f,%-5.0f %5.0fx%-5.0f" % p)
narrowest = min(p[3] for p in ps)
if os.environ["EXPECT"] == "deep":
    print("PASS smallest tile %.0f px wide, under the old floor" % narrowest if narrowest < 100 else "FAIL smallest tile still %.0f px wide" % narrowest)
else:
    print("PASS floor keeps tiles at %.0f px wide" % narrowest if narrowest >= 120 else "FAIL floor still allowed %.0f px wide" % narrowest)
PY
pkill -P "$$" -x alacritty
