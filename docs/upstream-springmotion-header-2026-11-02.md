# Upstream: install KWin's SpringMotion header (2026-11-02)

## The gap

KWin 6.8 moved its mass-spring-damper out of the slide plugin into the effect API (`src/effect/springmotion.{h,cpp}`) and exports `KWin::SpringMotion` from libkwin; the slide, overview and popup animations run on it. The class is `KWIN_EXPORT`ed, but its header is not on the install list:

```
src/CMakeLists.txt:  install(FILES
    effect/animationeffect.h
    effect/effect.h
    ...
    effect/quickeffect.h
    effect/timeline.h        <- springmotion.h belongs here
    effect/xcb.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwin/effect COMPONENT Devel)
```

So `/usr/include/kwin/effect/` has no `springmotion.h` and an out-of-tree effect cannot `#include` an exported class. Checked on v6.8.2 (what ships with Plasma 6.8) and on current `master`: both omit it, so this is not a 6.8-only slip.

## What Krellis does meanwhile

`effect/kwin_springmotion.h` declares the ABI Krellis links against: the public methods it calls, plus an opaque body sized to match KWin's real object (88 bytes, align 8, non-polymorphic, trivially destructible — measured by compiling against KWin's own header). `static_assert`s fail the build if a future KWin changes that size or makes the class polymorphic, so the failure mode is a compile error rather than corrupted memory.

That works, and it is the only reason Krellis carries a hand-maintained ABI declaration at all. It has to be re-checked on every KWin bump, and Krellis is ABI-locked to one KWin per build anyway, so the tax is small but permanent.

## The patch

`patches/kwin-6.8-install-springmotion-header.patch` — one added line to the effect header install list, so `springmotion.h` lands in `${includedir}/kwin/effect/` with the Devel component.

Verified: applies cleanly to KWin v6.8.2 `src/CMakeLists.txt` (`patch -p1 --dry-run`), and to current master, which lists the same headers.

## Filing it

- Repo: `invent.kde.org/plasma/kwin`, against `master`.
- Branch name suggestion: `install-springmotion-header`; commit subject: `Install the SpringMotion header`.
- Justification for the MR description: the class is exported from libkwin and part of the effect API (`KWIN_EXPORT`, `src/effect/springmotion.cpp` is built into the library), KWin's own animations use it, and out-of-tree effects get `-Werror=undefined-symbol` at load time rather than at build time today. Adding it to the Devel install list changes no behaviour.
- Risk: none beyond the installed-header list; the header only includes `kwin_export.h`, `<QtGlobal>` and `<chrono>`.
- Not done from this checkout: filing needs Ivan's KDE identity, so the patch and this note are the deliverable.

## Dropping the shim once it ships

1. Confirm the built KWin installs it: `pacman -Ql kwin | grep springmotion.h` (Arch) or check `${includedir}/kwin/effect/springmotion.h` after building from source.
2. In `effect/kwin_spring.h`, replace `#include "kwin_springmotion.h"` with `#include <kwin/effect/springmotion.h>`.
3. Delete `effect/kwin_springmotion.h` and the note about it in `README.md`'s "KWin 6.8" section and in `AGENTS.md`.
4. Rebuild and check nothing is duplicated: `nm -C build-release/root/kwin/effects/plugins/krellis.so | grep SpringMotion` should show only undefined (`U`) references, never a defined constructor.
5. `ctest` and the motion scenarios (`motion-feedback`, `hyprland`) as usual.

## Ready-to-paste MR text

Commit subject:

```
Install the SpringMotion header
```

Commit body:

```
SpringMotion became part of the effect API in 6.8: it moved out of the slide
plugin into src/effect/, it is exported from libkwin (KWIN_EXPORT) and KWin's
own animations run on it. Its header is not in the list of effect headers
installed for development, so an out-of-tree effect cannot #include a class
the library already exports - it either re-declares the ABI by hand or
duplicates the simulation.

Adding it to the existing install(FILES ...) block costs nothing at runtime:
the header only includes kwin_export.h, <QtGlobal> and <chrono>.
```

MR description:

```
## Problem

`KWin::SpringMotion` (`src/effect/springmotion.h`) is exported from libkwin but
not installed with the other effect headers (`src/CMakeLists.txt`, Devel
component). Out-of-tree effects that want KWin's own spring therefore cannot
include it, even though the symbol is part of the public effect surface.

## Solution

Add `effect/springmotion.h` to the effect header install list.

## Testing

- Builds unchanged; the file is only added to an `install(FILES ...)` list.
- Verified that `effect/springmotion.h` compiles standalone against the
  installed `kwin_export.h` and pulls in nothing but Qt and `<chrono>`.
- With the header installed, an out-of-tree effect can drop its local ABI
  declaration and `#include <kwin/effect/springmotion.h>` directly.
```
