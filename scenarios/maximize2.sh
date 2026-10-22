#!/usr/bin/env bash
# Run with NEST_OUTPUTS=2. Native maximize of one window must not push its neighbours onto the other output.
set -uo pipefail
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET; unset DISPLAY
call() { qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect."$@"; }
trap 'pkill -P "$$" -x alacritty || true' EXIT
for n in A B C; do alacritty --title "win-$n" -e sleep 600 >/dev/null 2>&1 & sleep 0.8; done
sleep 1
place() {
    local title=$1 dest=$2 source index snapshot
    for source in 0 1; do
        snapshot=$(call dumpOutput "$source")
        index=$(awk -v title="'$title'" '/ screen / { if (index($0,title)) { print n+0; exit } n++ }' <<<"$snapshot")
        if [ -n "$index" ]; then call selectOutput "$source"; call moveToOutput "$index" "$dest"; return; fi
    done
    echo "missing $title"; exit 1
}
for n in A B C; do place win-$n 0; done
call selectOutput 0; call overview; sleep 1
counts() { echo "out0=$(call dumpOutput 0 | grep -c ' screen ') out1=$(call dumpOutput 1 | grep -c ' screen ')"; }
echo "before: $(counts)"
cat >"$NEST_WORK/max.js" <<'JS'
for (const w of workspace.windowList()) if (w.caption === 'win-B') w.setMaximize(true, true);
JS
run() { local n; n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/max.js" "$1"); qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null; }
run m1; sleep 2
echo "maximized: $(counts)"; call dumpOutput 0 | head -5; call dumpOutput 1 | head -3
run m2; sleep 2
echo "restored: $(counts)"; call dumpOutput 0 | head -5
rc=0
[ "$(counts)" = "out0=3 out1=0" ] || { echo "FAIL windows left output 0: $(counts)"; rc=1; }
exit $rc
