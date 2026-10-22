#!/usr/bin/env bash
# Build and install Krellis (KWin effect + settings module) from this checkout.
#
#   ./install.sh                 build and install to /usr (asks for sudo)
#   ./install.sh --enable        ...and switch the effect on in KWin
#   ./install.sh --uninstall     remove what a previous install put down
#   ./install.sh --check         does the installed plugin match the KWin that is installed?
#
# The plugin links against KWin's private API, so it only loads into the KWin it was built against.
# Run this again after every KWin upgrade, or let a pacman hook do it (--pacman-hook).
set -euo pipefail

tested_kwin=6.7
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build=${BUILD_DIR:-$project/build-release}
prefix=/usr
kcm=ON
enable=0
uninstall=0
check=0
do_build=1
do_install=1
hook=""
remove_hook=0

usage() {
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'USAGE'

Options:
  --prefix DIR   install prefix (default /usr; KWin only searches the system Qt plugin path)
  --no-kcm       skip the System Settings module
  --enable       enable the effect in kwinrc and load it into the running KWin
  --uninstall    remove the installed files and disable the effect
  --check        compare the installed plugin with the installed KWin (exit 0 match, 1 mismatch, 2 not installed)
  --build-only   configure and build, install nothing
  --install-only install a build made earlier by --build-only
  --pacman-hook[=MODE]
                 Arch/CachyOS: after pacman upgrades kwin, deal with the plugin. MODE is "warn" (default: print what
                 to do) or "rebuild" (rebuild from this checkout as the current user, then install it)
  --no-pacman-hook
                 remove that hook
  -h, --help     this text

Environment: CMAKE_ARGS (extra cmake arguments), BUILD_DIR, DESTDIR (stage instead of installing for real).
USAGE
}

while [ $# -gt 0 ]; do
    case $1 in
        --prefix) prefix=${2:?--prefix needs a directory}; shift ;;
        --no-kcm) kcm=OFF ;;
        --enable) enable=1 ;;
        --uninstall) uninstall=1 ;;
        --check) check=1 ;;
        --build-only) do_install=0 ;;
        --install-only) do_build=0 ;;
        --pacman-hook) hook=warn ;;
        --pacman-hook=*) hook=${1#*=}; case $hook in warn|rebuild) ;; *) echo "--pacman-hook takes warn or rebuild" >&2; exit 2 ;; esac ;;
        --no-pacman-hook) remove_hook=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

# Run a command as root unless that is not needed (staged install, or the target is writable).
as_root() {
    if [ -n "${DESTDIR:-}" ] || [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi
}

qdbus_cmd() { command -v qdbus6 || command -v qdbus-qt6 || true; }

plugin_dir() {
    local d
    for q in qtpaths6 /usr/lib/qt6/bin/qtpaths6 /usr/lib/qt6/bin/qtpaths /usr/lib64/qt6/bin/qtpaths6; do
        if command -v "$q" >/dev/null 2>&1; then
            d=$("$q" --plugin-dir 2>/dev/null || true)
            [ -n "$d" ] && { echo "$d"; return; }
        fi
    done
    for q in qmake6 /usr/lib/qt6/bin/qmake6 /usr/lib/qt6/bin/qmake; do
        if command -v "$q" >/dev/null 2>&1; then
            d=$("$q" -query QT_INSTALL_PLUGINS 2>/dev/null || true)
            [ -n "$d" ] && { echo "$d"; return; }
        fi
    done
    for d in /usr/lib/qt6/plugins /usr/lib64/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins /usr/lib/aarch64-linux-gnu/qt6/plugins; do
        [ -d "$d" ] && { echo "$d"; return; }
    done
    die "cannot find the Qt 6 plugin directory (install qt6-base's tools, or pass -DKRELLIS_PLUGIN_DIR via CMAKE_ARGS)"
}

hook_file=${DESTDIR:-}/etc/pacman.d/hooks/krellis-kwin.hook
hook_script=${DESTDIR:-}/usr/lib/krellis/kwin-upgrade

running_kwin() {
    command -v kwin_wayland >/dev/null 2>&1 && kwin_wayland --version 2>/dev/null | awk '{print $NF}' | grep -o '^[0-9][0-9.]*' || true
}

# 0: the installed plugin was built for the installed KWin; 1: it was not; 2: not installed, or a version is unknown.
check_plugin() {
    local so built running
    so=$(plugin_dir)/kwin/effects/plugins/krellis.so
    [ -f "${DESTDIR:-}$so" ] || { echo "Krellis is not installed ($so)"; return 2; }
    built=$(grep -a -o 'krellis-built-for-kwin=[0-9.]*' "${DESTDIR:-}$so" | head -1 | cut -d= -f2 || true)
    running=$(running_kwin)
    if [ -z "$built" ] || [ -z "$running" ]; then echo "cannot tell: plugin built for '${built:-?}', KWin '${running:-?}'"; return 2; fi
    if [ "$built" = "$running" ]; then echo "Krellis matches KWin $running"; return 0; fi
    echo "Krellis was built for KWin $built but KWin $running is installed: run $project/install.sh"
    return 1
}

install_pacman_hook() {
    local mode=$1
    command -v pacman >/dev/null 2>&1 || [ -n "${DESTDIR:-}" ] || die "--pacman-hook is for Arch-based systems (no pacman here)"
    local tmp; tmp=$(mktemp -d)
    cat >"$tmp/hook" <<HOOK
[Trigger]
Operation = Install
Operation = Upgrade
Type = Package
Target = kwin

[Action]
Description = Krellis: checking the effect against the new KWin
When = PostTransaction
Exec = /usr/lib/krellis/kwin-upgrade
HOOK
    # The script is generated so that it carries this checkout's path and the build environment of the user who ran this.
    cat >"$tmp/script" <<SCRIPT
#!/bin/sh
# Written by Krellis' install.sh --pacman-hook=$mode. Remove with ./install.sh --no-pacman-hook.
src='$project'
owner='$(id -un)'
mode='$mode'
build_path='$PATH'
cmake_args='${CMAKE_ARGS:-}'
SCRIPT
    cat >>"$tmp/script" <<'SCRIPT'
status=$("$src/install.sh" --check 2>&1); rc=$?
[ "$rc" -eq 0 ] && exit 0
echo "Krellis: $status"
[ "$rc" -eq 1 ] || exit 0
if [ "$mode" != rebuild ]; then
    echo "Krellis: it will not load until it is rebuilt: $src/install.sh"
    exit 0
fi
home=$(getent passwd "$owner" | cut -d: -f6)
echo "Krellis: rebuilding it for the new KWin as $owner (this takes a minute)"
if runuser -u "$owner" -- env HOME="$home" PATH="$build_path" CMAKE_ARGS="$cmake_args" "$src/install.sh" --build-only >/tmp/krellis-rebuild.log 2>&1     && env PATH="$build_path" "$src/install.sh" --install-only >>/tmp/krellis-rebuild.log 2>&1; then
    echo "Krellis: rebuilt. Log out and back in to load it."
else
    echo "Krellis: the rebuild failed, see /tmp/krellis-rebuild.log. Run $src/install.sh by hand."
fi
exit 0
SCRIPT
    as_root install -D -m 0644 "$tmp/hook" "$hook_file"
    as_root install -D -m 0755 "$tmp/script" "$hook_script"
    rm -rf "$tmp"
    echo "Installed the pacman hook ($mode): $hook_file"
}

if [ "$check" -eq 1 ]; then
    check_plugin
    exit $?
fi

if [ "$remove_hook" -eq 1 ] && [ "$uninstall" -eq 0 ]; then
    as_root rm -f -- "$hook_file" "$hook_script"
    echo "Removed the pacman hook."
    exit 0
fi

if [ "$uninstall" -eq 1 ]; then
    as_root rm -f -- "$hook_file" "$hook_script"
    if [ -z "${DESTDIR:-}" ]; then
        say "Turning the effect off"
        if q=$(qdbus_cmd) && [ -n "$q" ]; then "$q" org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect krellis >/dev/null 2>&1 || true; fi
        command -v kwriteconfig6 >/dev/null 2>&1 && kwriteconfig6 --file kwinrc --group Plugins --key krellisEnabled false || true
    fi
    say "Removing installed files"
    if [ -f "$build/install_manifest.txt" ]; then
        # With DESTDIR the manifest already lists the staged paths.
        while IFS= read -r f || [ -n "$f" ]; do
            case $f in "${DESTDIR:-/nonexistent}"/*) ;; *) f=${DESTDIR:-}$f ;; esac
            as_root rm -f -- "$f"
        done <"$build/install_manifest.txt"
    else
        plugins=$(plugin_dir)
        as_root rm -f -- "${DESTDIR:-}$plugins/kwin/effects/plugins/krellis.so" \
            "${DESTDIR:-}$plugins/kwin/effects/configs/kcm_krellis.so" \
            "${DESTDIR:-}$plugins/plasma/kcms/systemsettings/kcm_krellis.so" \
            "${DESTDIR:-}$prefix/share/applications/kcm_krellis.desktop"
    fi
    echo "Removed. Your settings (~/.config/krellisrc) and saved layouts (~/.local/state/krellis) are left alone."
    echo "Log out and back in (or restart KWin) so the running compositor lets go of the plugin."
    exit 0
fi

plugins=$(plugin_dir)

if [ "$do_build" -eq 1 ]; then
    say "Checking the build tools"
    missing=()
    for tool in cmake c++; do command -v "$tool" >/dev/null 2>&1 || missing+=("$tool"); done
    generator=Ninja
    if ! command -v ninja >/dev/null 2>&1; then
        if command -v make >/dev/null 2>&1; then generator="Unix Makefiles"; else missing+=("ninja or make"); fi
    fi
    if [ ${#missing[@]} -gt 0 ]; then
        die "missing: ${missing[*]}. See the Requirements section of README.md for package names."
    fi

    if command -v kwin_wayland >/dev/null 2>&1; then
        kwin_version=$(kwin_wayland --version 2>/dev/null | awk '{print $NF}' || true)
        case $kwin_version in
            "$tested_kwin".*) say "KWin $kwin_version" ;;
            *) echo "warning: KWin $kwin_version found; Krellis is tested against KWin $tested_kwin.x. It may fail to build or load." >&2 ;;
        esac
    fi

    say "Configuring (Release, plugins into $plugins)"
    # shellcheck disable=SC2086
    cmake -S "$project" -B "$build" -G "$generator" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DBUILD_TESTING=OFF -DKRELLIS_BUILD_EFFECT=ON -DKRELLIS_BUILD_KCM="$kcm" -DKRELLIS_PLUGIN_DIR="$plugins" ${CMAKE_ARGS:-} \
        || die "configure failed. Missing development packages are the usual cause; see the Requirements section of README.md."

    say "Building"
    targets=(krellis)
    [ "$kcm" = ON ] && targets+=(kcm_krellis)
    cmake --build "$build" --target "${targets[@]}"
fi

if [ "$do_install" -eq 0 ]; then
    say "Built in $build, nothing installed"
    exit 0
fi
[ -f "$build/cmake_install.cmake" ] || die "nothing to install: there is no build in $build (run ./install.sh first)"

say "Installing to ${DESTDIR:-}$prefix"
# Install onto a new file, not over the old one: a running KWin has the old plugin mapped, and overwriting that
# file in place can take the compositor down.
as_root rm -f -- "${DESTDIR:-}$plugins/kwin/effects/plugins/krellis.so"
as_root cmake --install "$build"
[ -n "$hook" ] && install_pacman_hook "$hook"

if [ -n "${DESTDIR:-}" ]; then
    say "Staged under $DESTDIR, nothing was enabled"
    exit 0
fi

if [ "$enable" -eq 1 ]; then
    say "Enabling the effect"
    kwriteconfig6 --file kwinrc --group Plugins --key krellisEnabled true
    if q=$(qdbus_cmd) && [ -n "$q" ]; then
        "$q" org.kde.KWin /KWin org.kde.KWin.reconfigure >/dev/null 2>&1 || true
        "$q" org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect krellis >/dev/null 2>&1 || true
    fi
fi

cat <<NEXT

Installed Krellis. KWin keeps a plugin file it has already loaded for the life of the session, so if an older
Krellis is running, log out and back in to get this build.
NEXT
if [ "$enable" -eq 0 ]; then
    cat <<'NEXT'

Turn it on in System Settings > Window Management > Desktop Effects (Krellis), or run:
  ./install.sh --enable
Meta+Alt+Escape turns it off again at any time.
NEXT
fi
