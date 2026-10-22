#!/usr/bin/env bash
# Build and install Krellis (KWin effect + settings module) from this checkout.
#
#   ./install.sh                 build and install to /usr (asks for sudo)
#   ./install.sh --enable        ...and switch the effect on in KWin
#   ./install.sh --uninstall     remove what a previous install put down
#
# The plugin links against KWin's private API, so it only loads into the KWin it was built against.
# Run this again after every KWin upgrade.
set -euo pipefail

tested_kwin=6.7
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build=${BUILD_DIR:-$project/build-release}
prefix=/usr
kcm=ON
enable=0
uninstall=0

usage() {
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'USAGE'

Options:
  --prefix DIR   install prefix (default /usr; KWin only searches the system Qt plugin path)
  --no-kcm       skip the System Settings module
  --enable       enable the effect in kwinrc and load it into the running KWin
  --uninstall    remove the installed files and disable the effect
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

if [ "$uninstall" -eq 1 ]; then
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

plugins=$(plugin_dir)
say "Configuring (Release, plugins into $plugins)"
# shellcheck disable=SC2086
cmake -S "$project" -B "$build" -G "$generator" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DBUILD_TESTING=OFF -DKRELLIS_BUILD_EFFECT=ON -DKRELLIS_BUILD_KCM="$kcm" -DKRELLIS_PLUGIN_DIR="$plugins" ${CMAKE_ARGS:-} \
    || die "configure failed. Missing development packages are the usual cause; see the Requirements section of README.md."

say "Building"
targets=(krellis)
[ "$kcm" = ON ] && targets+=(kcm_krellis)
cmake --build "$build" --target "${targets[@]}"

say "Installing to ${DESTDIR:-}$prefix"
as_root cmake --install "$build"

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
