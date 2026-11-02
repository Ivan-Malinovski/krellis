#!/usr/bin/env bash
# Is Krellis actually running, and is it the build we meant to run? Read-only on purpose: nothing here writes,
# reloads or needs root, so it is safe from a login autostart, a systemd user unit and an SSH shell.
#
#   tools/healthcheck.sh            PASS/FAIL per check; exit 0 (all pass) or 1 (any fail)
#   tools/healthcheck.sh --quiet    print nothing when everything passes
#   tools/healthcheck.sh --notify   ...and on failure send one desktop notification
#   tools/healthcheck.sh --wait 30  ...but first wait up to 30 s for KWin's D-Bus name (for a login autostart)
#   tools/healthcheck.sh --help
#
# Why it exists: the effect is ABI-locked to the KWin it was built against. When Plasma upgraded KWin 6.7.5 to
# 6.8.2, kwinrc still said krellisEnabled=true and the .so was there, but nothing was loaded, so the desktop
# quietly lost tiling, its panel bar and its shortcuts. This is the signal that was missing.
#
# Checks (each a PASS/FAIL line with a short reason and, on failure, the command that fixes it):
#   1. a krellis id is in KWin's loadedEffects            (D-Bus org.kde.KWin /Effects)
#   2. the running build stamp is the newest installed .so (D-Bus /Krellis ...version vs the file)
#   3. that .so was built for the installed KWin           (krellis-built-for-kwin= vs kwin_wayland --version)
#   4. kwinrc [Plugins] <installed id>Enabled=true         (install.sh --enable writes the base "krellis" key)
# Reported but never failing: other (inert) krellis*.so files, and the pacman hook.
#
# Environment overrides, so the failure paths can be exercised without touching the live session:
#   KRELLIS_HEALTH_PLUGIN_DIR       dir holding krellis*.so
#                                   (default /usr/lib/qt6/plugins/kwin/effects/plugins)
#   KRELLIS_HEALTH_LOADED           skip the D-Bus loadedEffects call; use this comma-separated list
#   KRELLIS_HEALTH_RUNNING_VERSION  skip the D-Bus version call; use this string, e.g. 'build Nov  2 2026 21:31:59'
#   KRELLIS_HEALTH_KWIN_VERSION     skip 'kwin_wayland --version'; use this version, e.g. '6.8.2'
#   KRELLIS_HEALTH_KWINRC           kwinrc to read (default: kwinrc, i.e. $XDG_CONFIG_HOME/kwinrc)
#
# To see the "KWin has no D-Bus name" failure without touching the session, point the bus at nothing:
#   DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent tools/healthcheck.sh
set -euo pipefail

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
plugins=${KRELLIS_HEALTH_PLUGIN_DIR:-/usr/lib/qt6/plugins/kwin/effects/plugins}
hook=/etc/pacman.d/hooks/krellis-kwin.hook
kwinrc_file=${KRELLIS_HEALTH_KWINRC:-${XDG_CONFIG_HOME:-$HOME/.config}/kwinrc}

usage() { sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

quiet=0
notify=0
wait_s=0
while [ $# -gt 0 ]; do
    arg=$1; shift
    case $arg in
        --quiet) quiet=1 ;;
        --notify) notify=1 ;;
        --wait) wait_s=${1:-}; shift || true ;;
        --wait=*) wait_s=${arg#--wait=} ;;
        -h|--help) usage; exit 0 ;;
        *) echo "usage: healthcheck.sh [--quiet] [--notify] [--wait seconds] [--help]" >&2; exit 2 ;;
    esac
done
case $wait_s in ''|*[!0-9]*) echo "--wait takes a number of seconds" >&2; exit 2 ;; esac

lines=()
fails=0
pass() { lines+=("PASS  $*"); }
fail() { lines+=("FAIL  $*"); fails=$((fails + 1)); }
fix() { lines+=("      fix: $*"); }
info() { lines+=("info  $*"); }

# A login autostart can run before KWin has taken its D-Bus name: wait for it rather than reporting a false failure.
if [ "$wait_s" -gt 0 ] && [ -z "${KRELLIS_HEALTH_LOADED+set}" ]; then
    for _ in $(seq "$wait_s"); do
        qdbus6 org.kde.KWin /KWin >/dev/null 2>&1 && break
        sleep 1
    done
fi

# --- what is loaded, and what is installed -------------------------------------------------------------------
loaded_raw=""
dbus_answer=1
if [ -n "${KRELLIS_HEALTH_LOADED+set}" ]; then
    loaded_raw=$KRELLIS_HEALTH_LOADED
elif ! command -v qdbus6 >/dev/null 2>&1; then
    dbus_answer=0
elif ! loaded_raw=$(qdbus6 org.kde.KWin /Effects org.freedesktop.DBus.Properties.Get \
    org.kde.kwin.Effects loadedEffects 2>/dev/null); then
    dbus_answer=0
    loaded_raw=""
fi
# qdbus6 prints the list one id per line; the D-Bus wire form is comma-separated, so accept both.
# Same id shape live.sh accepts (krellis, krellis_<stamp>, and the old trellis ids).
loaded_ids=$(printf '%s\n' "$loaded_raw" | tr ',' '\n' | sed 's/[[:space:]]//g' | grep -E '^[kt]rellis(_[0-9]+)?$' || true)
loaded=$(printf '%s\n' "$loaded_ids" | head -1)

newest=$(ls -t "$plugins"/krellis*.so 2>/dev/null | head -1 || true)
newest_id=""
[ -n "$newest" ] && newest_id=$(basename "$newest" .so)

running=${KRELLIS_HEALTH_RUNNING_VERSION-}
if [ -z "${KRELLIS_HEALTH_RUNNING_VERSION+set}" ]; then
    running=$(qdbus6 org.kde.KWin /Krellis org.kde.kwin.KWin.KrellisEffect.version 2>/dev/null || true)
fi
installed=""
built_for=""
if [ -n "$newest" ]; then
    installed=$(grep -a -o 'build [A-Z][a-z][a-z] [ 0-9][0-9] 20[0-9][0-9] [0-9:]*' "$newest" | head -1 || true)
    built_for=$(grep -a -o 'krellis-built-for-kwin=[0-9.]*' "$newest" | head -1 | cut -d= -f2 || true)
fi
kwin_version=${KRELLIS_HEALTH_KWIN_VERSION-}
if [ -z "${KRELLIS_HEALTH_KWIN_VERSION+set}" ]; then
    kwin_version=$(kwin_wayland --version 2>/dev/null | awk '{print $NF}' || true)
fi
kwin_version=${kwin_version#kwin }  # the override is hand-written, so accept 'kwin 6.8.2' as well as '6.8.2'

# --- 1. is a krellis effect loaded? --------------------------------------------------------------------------
if [ "$dbus_answer" -eq 0 ]; then
    fail "loaded: KWin has no D-Bus name (org.kde.KWin is not on this bus)"
    fix "check for an orphan compositor that stole the name: pgrep -a kwin_wayland | grep -- --virtual"
    fix "an orphan 'kwin_wayland --virtual' holds org.kde.KWin and org.kde.kglobalaccel, so KWin never"
    fix "registers them: every global shortcut is dead and logout silently does nothing. Close that process,"
    fix "then log out and back in. If no orphan is running, KWin itself is down: log in again."
elif [ -z "$loaded" ]; then
    fail "loaded: KWin answered but no krellis effect is loaded"
    if [ -z "$newest" ]; then
        fix "nothing is installed either: cd $project && ./install.sh"
    else
        fix "load the newest install for this session: $project/tools/live.sh on"
    fi
else
    pass "loaded: $loaded"
fi

# --- 2. is the running build the newest installed one? -------------------------------------------------------
if [ -z "$newest" ]; then
    fail "build: no krellis*.so in $plugins, so there is nothing to run"
    fix "cd $project && ./install.sh   # then log out and back in"
elif [ -z "$loaded" ]; then
    fail "build: Krellis is not loaded, so there is no running build to compare with $newest_id"
    fix "see the first failure: load it with $project/tools/live.sh on"
elif [ -z "$running" ] || [ -z "$installed" ]; then
    fail "build: cannot read a build stamp (running '${running:-unknown}', installed '${installed:-none}')"
    fix "the loaded effect predates version stamps, or the file is not a krellis build: run $project/tools/live.sh on"
elif [ "$running" != "$installed" ]; then
    fail "build: running '$running' but the newest installed is '$installed' ($newest_id)"
    fix "swap the running build for the installed one: $project/tools/live.sh on"
else
    pass "build: $running ($newest_id)"
fi

# --- 3. was that build made for the installed KWin? ----------------------------------------------------------
if [ -z "$newest" ]; then
    fail "kwin: cannot tell which KWin the plugin was built for (nothing installed)"
    fix "cd $project && ./install.sh   # then log out and back in"
elif [ -z "$built_for" ] || [ -z "$kwin_version" ]; then
    fail "kwin: cannot tell (plugin built for '${built_for:-?}', kwin_wayland '${kwin_version:-?}')"
    fix "cd $project && ./install.sh   # then log out and back in"
elif [ "$built_for" != "$kwin_version" ]; then
    fail "kwin: $newest_id was built for KWin $built_for but KWin $kwin_version is installed"
    fix "cd $project && ./install.sh   # then log out and back in"
    fix "the plugin is ABI-locked to KWin: it refuses to load into $kwin_version until it is rebuilt"
else
    pass "kwin: $newest_id was built for KWin $built_for (installed: $kwin_version)"
fi

# --- 4. does kwinrc enable the installed id? -----------------------------------------------------------------
# kreadconfig6 prints nothing and still exits 0 for a missing key, hence the empty check plus a grep fallback.
kwinrc_value() {
    command -v kreadconfig6 >/dev/null 2>&1 || return 0
    # kreadconfig6's default file is kdeglobals, so kwinrc has to be named explicitly.
    kreadconfig6 --file "${KRELLIS_HEALTH_KWINRC:-kwinrc}" --group Plugins --key "$1" 2>/dev/null || true
}
kwinrc_enabled() { # <id> -> true/false/empty
    local v
    v=$(kwinrc_value "${1}Enabled")
    if [ -z "$v" ]; then
        v=$(grep -sE "^[[:space:]]*${1}Enabled[[:space:]]*=" "$kwinrc_file" | tail -1 | sed 's/.*=[[:space:]]*//' | tr -d '[:space:]' || true)
    fi
    printf '%s' "$v"
}
id=${loaded:-$newest_id}
if [ -z "$id" ]; then
    fail "kwinrc: cannot tell which id to look for (nothing installed, nothing loaded)"
    fix "cd $project && ./install.sh --enable"
elif [ "$(kwinrc_enabled "$id")" = true ]; then
    pass "kwinrc: [Plugins] ${id}Enabled=true"
elif [[ $id =~ ^krellis_[0-9]+$ ]] && [ "$(kwinrc_enabled krellis)" = true ]; then
    # install.sh --enable writes the base id, and the stamped ids are that same effect under a fresh name.
    pass "kwinrc: [Plugins] krellisEnabled=true (base id; $id has no key of its own)"
    info "a KWin restart in this session reloads the plain krellis.so; tools/live.sh login handles the login case"
else
    fail "kwinrc: [Plugins] ${id}Enabled is not true, so KWin will not bring Krellis back after a restart or login"
    fix "cd $project && ./install.sh --enable"
fi

# --- extra information, never fatal --------------------------------------------------------------------------
stale=""
if [ -n "$newest" ]; then
    for f in "$plugins"/krellis*.so; do
        [ -e "$f" ] || continue
        [ "$f" = "$newest" ] && continue
        stale+=" $(basename "$f")"
    done
fi
[ -n "$stale" ] && info "other installed plugin files (inert, just clutter):${stale}"
[ -e "$hook" ] && info "pacman hook: $hook present" || info "pacman hook: $hook absent (install.sh --pacman-hook adds it)"

# --- report --------------------------------------------------------------------------------------------------
if [ "$fails" -gt 0 ]; then
    lines+=("krellis healthcheck: $fails check(s) failed")
else
    lines+=("krellis healthcheck: ok")
fi
if [ "$quiet" -eq 0 ] || [ "$fails" -gt 0 ]; then
    [ ${#lines[@]} -gt 0 ] && printf '%s\n' "${lines[@]}"
fi

if [ "$fails" -gt 0 ] && [ "$notify" -eq 1 ]; then
    text=$(printf '%s\n' "${lines[@]}" | grep -E '^(FAIL|      fix:)' || true)
    [ -n "$text" ] || text='Krellis is not running'
    if command -v kdialog >/dev/null 2>&1; then
        kdialog --title Krellis --error "$text" >/dev/null 2>&1 || true
    elif command -v notify-send >/dev/null 2>&1; then
        notify-send -u critical Krellis "$text" >/dev/null 2>&1 || true
    fi
fi

[ "$fails" -eq 0 ] && exit 0
exit 1
