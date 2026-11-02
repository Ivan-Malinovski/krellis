#!/usr/bin/env bash
# Runs scenarios in nested KWins, several at a time, and prints one line per scenario.
#
#   tools/scenarios.sh                  every scenario in scenarios/
#   tools/scenarios.sh tabs pin drag    just these (names without .sh)
#   tools/scenarios.sh -j 4 -k          four at a time; -k keeps the work dirs of passing runs too
#
# Work dirs: /tmp/krellis-scn.XXXXXX/<name>/ (out/log.txt, out/kwin.log) plus /tmp/krellis-scn.XXXXXX/<name>.out.
# A failed, crashed or timed-out scenario always keeps its work dir; a passing one is removed unless -k was
# given. The whole temp dir is removed at the end only when it is empty, so -k (or an interrupted run, or a
# run killed mid-flight, which keeps nothing but leaves the tree behind) leaves /tmp/krellis-scn.* to clean up.
#
# Per scenario: scenarios/<name>.rc seeds krellisrc (the nested default is only glide=true and a few quiet switches),
# scenarios/<name>.env can override the seed, set EXPECT=..., choose NEST_EFFECTS (default krellis) or NEST_SIZE ("W H", default "1280 800").
# 'NEST_OUTPUTS=2' in the script's header gives it two outputs.
#
# A scenario counts as failed when its log has a FAIL line, when its KWin segfaulted, or when it timed out.
# Scenarios that only print state (no PASS/FAIL lines) show as "-" and are not judged.
# Core dumps are switched off for the runs, so a crashing nested KWin does not raise crash-watch notifications.
# Needs a build in build/ (cmake --build build refreshes build/root). Never touches your session or ~/.config.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
jobs=6 keep=0 timeout_s=240
while getopts "j:kt:" opt; do
    case $opt in
        j) jobs=$OPTARG ;;
        k) keep=1 ;;
        t) timeout_s=$OPTARG ;;
        *) echo "usage: $0 [-j jobs] [-k] [-t seconds] [scenario...]" >&2; echo "  -j N  run N scenarios at a time (default 6); use -j 1 when debugging" >&2; echo "  -k    also keep the work dirs of passing scenarios (failed/crashed/timed-out ones are always kept)" >&2; echo "  -t N  per-scenario timeout in seconds (default 240)" >&2; echo "  Work dirs live under /tmp/krellis-scn.* and are only removed when none is kept." >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -d "$root/build/root" ] || { echo "no build/root: run cmake --build build first" >&2; exit 2; }

names=("$@")
if [ ${#names[@]} -eq 0 ]; then
    for f in "$root"/scenarios/*.sh; do names+=("$(basename "$f" .sh)"); done
fi
for n in "${names[@]}"; do
    [ -f "$root/scenarios/$n.sh" ] || { echo "no such scenario: $n" >&2; exit 2; }
done

# Refuse to start any scenario while a foreign compositor owns org.kde.KWin on the session bus. Every
# scenario's nested KWin gets its own bus from dbus-run-session, but a stray kwin_wayland started with
# --virtual (or with its own --socket) on the user bus holds org.kde.KWin and org.kde.kglobalaccel; KWin's
# GlobalShortcutsManager::init() then deletes its KGlobalAccelD for the life of that process with no retry,
# so every global shortcut in the real session is dead and plasma-shutdown cannot log out
# ("KWin failed to complete logout"). Exit status 1 means "dangerous owner"; no scenario has been started then.
# Tests only: KRELLIS_TEST_BUS_OWNER_CMDLINE=<cmdline> replaces the detected owner command line.
guard_session_bus() {
    local pid line cmdline sock w i to="" dangerous=0
    local -a words
    if [ -n "${KRELLIS_TEST_BUS_OWNER_CMDLINE:-}" ]; then
        pid=test; cmdline=$KRELLIS_TEST_BUS_OWNER_CMDLINE
    else
        command -v busctl >/dev/null 2>&1 || command -v qdbus6 >/dev/null 2>&1 || return 0
        command -v timeout >/dev/null 2>&1 && to="timeout 5"
        if command -v busctl >/dev/null 2>&1; then
            line=$($to busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
                org.freedesktop.DBus GetConnectionUnixProcessID s org.kde.KWin 2>/dev/null) || line=""
        else
            line=$($to qdbus6 org.freedesktop.DBus / \
                org.freedesktop.DBus.GetConnectionUnixProcessID org.kde.KWin 2>/dev/null) || line=""
        fi
        pid=$(printf '%s\n' "$line" | awk '{print $NF}')
        case $pid in ''|*[!0-9]*) return 0 ;; esac  # nobody owns org.kde.KWin, or nothing to parse
        cmdline=$(tr '\0' ' ' 2>/dev/null <"/proc/$pid/cmdline" || true)
        if [ -z "${cmdline// /}" ]; then
            # Another user's (or a zombie) process: we cannot tell what it is, so warn but do not block.
            echo "warning: org.kde.KWin on the session bus is owned by pid $pid, whose /proc/$pid/cmdline is not readable." >&2
            echo "warning: if that is a stray kwin_wayland, global shortcuts and logout are broken until it exits." >&2
            return 0
        fi
    fi
    read -ra words <<<"$cmdline"
    sock=""
    for ((i = 0; i < ${#words[@]}; i++)); do
        case ${words[i]} in
            --socket) [ $((i + 1)) -lt ${#words[@]} ] && sock=${words[i + 1]} ;;
            --socket=*) sock=${words[i]#--socket=} ;;
        esac
    done
    case " $cmdline " in
        *" --virtual "*) dangerous=1 ;;
        *)
            # Only compare when we know this session's socket: over SSH WAYLAND_DISPLAY is unset and a
            # healthy desktop KWin would otherwise look foreign.
            if [ -n "$sock" ] && [ -n "${WAYLAND_DISPLAY:-}" ] && [ "$sock" != "$WAYLAND_DISPLAY" ]; then dangerous=1; fi
            ;;
    esac
    [ "$dangerous" = 1 ] || return 0  # plain desktop KWin (no --virtual, no foreign socket): healthy
    { echo "ERROR: a foreign compositor owns org.kde.KWin on this session bus."
      echo "  pid $pid: $cmdline"
      echo "It was not started with --socket \"\$WAYLAND_DISPLAY\" (${WAYLAND_DISPLAY:-unset}) and/or runs --virtual, so it is not your desktop KWin."
      echo "While it lives it holds org.kde.KWin and org.kde.kglobalaccel, KWin drops its KGlobalAccelD for good"
      echo "(no retry: every global shortcut in the real session dies) and plasma-shutdown cannot log out."
      echo "Nested tests must run under dbus-run-session; kill the stray compositor before running scenarios."
    } >&2
    return 1
}
guard_session_bus || exit 1

tmp=$(mktemp -d /tmp/krellis-scn.XXXXXX)
run_one() {
    local n=$1 w="$tmp/$1" rc="" outputs start=$SECONDS
    [ -f "$root/scenarios/$n.rc" ] && rc="$root/scenarios/$n.rc"
    outputs=1; grep -q 'NEST_OUTPUTS=2' "$root/scenarios/$n.sh" && outputs=2
    ulimit -c 0
    export PATH="$HOME/.local/bin:$PATH"
    (
        cd "$root" || exit 1
        export NEST_KRELLISRC=$rc NEST_OUTPUTS=$outputs KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 NEST_BACKEND=virtual
        export NEST_SOCKET="krellis-scn-$$-$n" NEST_WORK=$w
        export NEST_EFFECTS=krellis
        # scenarios/<name>.env may override the seed (NEST_KRELLISRC=) or set EXPECT=... for a scenario with modes
        if [ -f "scenarios/$n.env" ]; then set -a; . "scenarios/$n.env"; set +a; fi
        exec timeout "$timeout_s" tools/nested.sh build/root "$NEST_EFFECTS" "scenarios/$n.sh" ${NEST_SIZE:-1280 800}
    ) >"$tmp/$n.out" 2>&1 </dev/null
    local code=$? log="$w/out/log.txt" pass fail segv=0 note=""
    pass=$(grep -cE '^(PASS|ok)' "$log" 2>/dev/null); fail=$(grep -c '^FAIL' "$log" 2>/dev/null)
    grep -q 'Segmentation fault\|KCrash: Application .kwin_wayland. crashing' "$tmp/$n.out" "$w/out/kwin.log" 2>/dev/null && segv=1
    [ "$code" = 124 ] && note=" TIMEOUT"
    [ "$segv" = 1 ] && note="$note CRASH"
    local verdict=ok
    if [ "$fail" -gt 0 ] || [ "$segv" = 1 ] || [ "$code" = 124 ]; then verdict=FAIL
    elif [ "$pass" -eq 0 ]; then verdict=-; fi
    printf '%-4s %-26s pass=%-3s fail=%-3s %3ss%s\n' "$verdict" "$n" "$pass" "$fail" "$((SECONDS - start))" "$note"
    if [ "$verdict" != FAIL ] && [ "$keep" = 0 ]; then rm -rf "$w" "$tmp/$n.out"; fi
}
export -f run_one
export root tmp keep timeout_s

started=$SECONDS
report=$(printf '%s\n' "${names[@]}" | xargs -P "$jobs" -I{} bash -c 'run_one {}' | sort -k1,1r -k2,2)
printf '%s\n---\n' "$report"
bad=$(printf '%s\n' "$report" | grep -c '^FAIL' || true)
echo "${#names[@]} scenarios, $bad failed, $((SECONDS - started))s"
if [ -n "$(ls "$tmp" 2>/dev/null)" ]; then
    echo "kept: $tmp/<name>/out/log.txt and $tmp/<name>.out"
else
    rmdir "$tmp"
fi
[ "$bad" = 0 ]
