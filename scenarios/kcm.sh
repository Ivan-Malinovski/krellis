#!/usr/bin/env bash
# The settings module is a second front end for krellisrc: its QML is only checked by a compositor actually
# instantiating it. A syntax error, a property that does not exist, or a required delegate role the C++ model
# does not provide all fail here and nowhere else — the C++ tests never load it, and a stale copy on disk looks
# perfectly fine until someone opens System Settings.
exec >"$NEST_OUT/log.txt" 2>&1
export WAYLAND_DISPLAY=$NEST_SOCKET
unset DISPLAY
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fi; }
# What QML says when it cannot build the page, however it says it.
qml_errors() { grep -hE 'main\.qml:[0-9]+:|Cannot assign|is not defined|Required property .* was not initialized|Unable to assign' "$@" 2>/dev/null | wc -l; }

check "kcmshell6 is installed" "$(command -v kcmshell6 >/dev/null && echo yes || echo no)" yes

kcmshell6 --smoke-test kcm_krellis >"$NEST_OUT/smoke.txt" 2>&1
check "the module instantiates" "$?" 0
check "loading the page raises no QML error" "$(qml_errors "$NEST_OUT/smoke.txt")" 0

kcmshell6 kcm_krellis >>"$NEST_OUT/kcm.txt" 2>&1 & shell=$!
sleep 6
cat >"$NEST_WORK/wins.js" <<'JS'
console.warn("KCMWIN " + JSON.stringify(workspace.windowList().map(w => w.caption)));
JS
n=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$NEST_WORK/wins.js" wins)
qdbus6 org.kde.KWin /Scripting/Script$n org.kde.kwin.Script.run >/dev/null 2>&1
sleep 1

check "the module opened its window" "$(grep -c 'KCMWIN .*"Krellis"' "$NEST_OUT/kwin.log")" 1
# The rules in scenarios/kcm.rc are what the page had to read; a delegate that cannot be created prints here.
check "building the page raises no QML error" "$(qml_errors "$NEST_OUT/kcm.txt" "$NEST_OUT/log.txt")" 0
cat "$NEST_OUT/kcm.txt"
"$KRELLIS_ROOT/tools/shot" kcm >/dev/null
check "the page rendered" "$([ -s "$NEST_OUT/kcm.png" ] && echo yes || echo no)" yes
kill "$shell" 2>/dev/null
