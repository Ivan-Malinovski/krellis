#!/usr/bin/env bash
# Build the small GPL-2.0-or-later Aurorae patch in isolation. No system installation or live reload.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=${AURORAE_FIX_WORK:-$root/build-aurorae-fix}
id=${AURORAE_FIX_ID:-org.kde.kwin.aurorae.krellis_$(date +%m%d%H%M%S)}
[[ "$id" =~ ^org\.kde\.kwin\.aurorae\.krellis_[0-9]+$ ]] || { echo 'Invalid plugin id' >&2; exit 1; }
mkdir -p "$work"
archive="$work/aurorae-6.7.5.tar.gz"
if [ ! -f "$archive" ]; then
    curl -fsSL https://codeload.github.com/KDE/aurorae/tar.gz/refs/tags/v6.7.5 -o "$archive"
fi
printf '%s  %s\n' 2481f979e3f0999aa8aab57be1664476d6c0caf71780e9b68030180fc5c661e0 "$archive" | sha256sum --check
# Re-extract the pinned upstream files so rerunning does not apply the patch twice.
tar -xzf "$archive" -C "$work"
source_dir="$work/aurorae-6.7.5"
patch --batch --directory "$source_dir" -p1 < "$root/patches/aurorae-6.7.5-render-cache.patch"
printf '\nset_target_properties(aurorae_v2 PROPERTIES LIBRARY_OUTPUT_NAME "%s")\n' "$id" >> "$source_dir/CMakeLists.txt"
cmake -S "$source_dir" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SKIP_RPATH=ON -DCMAKE_PREFIX_PATH="$root/third_party/ecm-prefix"
cmake --build "$work/build" --target aurorae_v2 --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
mkdir -p "$work/root/org.kde.kdecoration3"
install -m755 "$work/build/bin/$id.so" "$work/root/org.kde.kdecoration3/$id.so"
printf '%s\n' "$id" > "$work/plugin-id"
printf '\nBuilt %s\nStaged in %s/root; nothing installed or loaded.\n' "$id" "$work"
