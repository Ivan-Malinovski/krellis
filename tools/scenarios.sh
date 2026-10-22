#!/usr/bin/env bash
# Runs scenarios in nested KWins, several at a time, and prints one line per scenario.
#
#   tools/scenarios.sh                  every scenario in scenarios/
#   tools/scenarios.sh tabs pin drag    just these (names without .sh)
#   tools/scenarios.sh -j 4 -k          four at a time; -k keeps the work dirs of passing runs too
#
# Per scenario: scenarios/<name>.rc seeds krellisrc (the nested default is only glide=true and a few quiet switches),
# scenarios/<name>.env can override the seed or set EXPECT=..., and 'NEST_OUTPUTS=2' in the script's header gives it two outputs.
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
        *) echo "usage: $0 [-j jobs] [-k] [-t seconds] [scenario...]" >&2; exit 2 ;;
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
        # scenarios/<name>.env may override the seed (NEST_KRELLISRC=) or set EXPECT=... for a scenario with modes
        if [ -f "scenarios/$n.env" ]; then set -a; . "scenarios/$n.env"; set +a; fi
        exec timeout "$timeout_s" tools/nested.sh build/root krellis "scenarios/$n.sh" 1280 800
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
