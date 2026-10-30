# Deep zoom-out pauses in Aurorae's SVG render cache

The remaining large pause was captured in the compositor's main thread inside libpng, through `QImage::save` and `KLocalImageCacheImplementation::serializeImage`, invoked by the KSvg cache timer. It occurs between painting frames, explaining why the paint probe alone missed it. It is distinct from the earlier repeated overview-QML setup issue, whose fix remains useful.

KSvg batches rendered pixmaps and [flushes them on a delayed timer](https://github.com/KDE/ksvg/blob/master/src/ksvg/private/imageset_p.cpp). Deep navigation changes native sizes of SVG-decorated windows, yielding large full-frame pixmaps. The captured main-thread PNG compression blocks the desktop. Testing session writes on a worker did not change this pause; that experiment was fully reverted. A filesystem comparison helped expose the measurement gap but does not establish disk I/O as the cause.

Aurorae v6.7.5 owns its frame SVG objects privately as unparented unique pointers. The effect cannot safely reach them through QObject introspection or a public decoration API. The prepared fix belongs in Aurorae: [seven render-cache flag changes](../patches/aurorae-6.7.5-render-cache.patch), using KSvg's existing `setUsingRenderingCache(false)` API. The [Aurorae source](https://github.com/KDE/aurorae/blob/v6.7.5/v2/decoration.cpp) and [KSvg cache guard](https://github.com/KDE/ksvg/blob/master/src/ksvg/framesvg.cpp) show the affected path. The theme, title-bar transparency, geometry and button behavior are unchanged. Current frame pixmaps stay in memory; only the large size-history cache is bypassed. Small button SVG caching stays enabled.

## Measurement

Four serial runs use the same Krellis build, 25 opaque Kitty windows sharing a test-only instance, generated text that redraws on resize, the FieldworkDark decoration assets read-only, and a 3440×1440 virtual output. Each starts at a camera width of about .000294 and traverses 14 outward destinations with events 220 ms apart. A persistent D-Bus connection times compositor replies, without CLI startup costs.

| Run order | Worst heartbeat per cycle, ms |
| --- | --- |
| Original Aurorae | 782.93 / 360.99 / 6.00 |
| Patched Aurorae | 0.25 / 0.26 / 7.83 |
| Patched Aurorae, repeated first | 0.24 / 0.24 / 0.24 |
| Original Aurorae, repeated second | 777.94 / 355.91 / 0.23 |

Paint-frame maxima can still approach 20 ms; this removes the reproduced large delayed pause, not every possible dropped frame or a guarantee of physical 144 Hz smoothness. These results are offscreen. [Summaries and captured stack](artifacts/aurorae-render-cache-2026-10-30.json).

The earlier zoom-burst fixture sent events 60 ms apart; the navigator's 180 ms quiet threshold treats each burst as one wheel gesture. Its successful overlay-reuse tests cover repeated one-level transitions, not deep traversal. `deep-out` now asserts a genuinely small starting camera and at least six distinct destinations. It also uses Python's `dbus` module for persistent timing. A synthetic text client and optional real activation during spawning produce a reproducible deep dwindle chain.

## Build and activation

```
tools/build-aurorae-fix.sh
```

This downloads pinned KDE v6.7.5 sources into `build-aurorae-fix/`, applies the patch and stages a uniquely named decoration plugin in `build-aurorae-fix/root/org.kde.kdecoration3/`. It performs no privileged install and changes no settings. The external source/patch retains GPL-2.0-or-later licensing; Krellis's core/effect remains MIT. The output has no RPATH. This local plugin is built for the installed KDE/KDecoration version; rebuild or switch back after relevant KDE upgrades.

For a nested reproduction, set `NEST_AURORAE_THEME_DIR` to an existing SVG theme directory. The harness links its assets read-only into its private data directory and selects `NEST_DECORATION_LIBRARY` only in its private kwinrc. Set `QT_PLUGIN_PATH=$PWD/build-aurorae-fix/root` and select the ID from `build-aurorae-fix/plugin-id` for the patched control. The live session is untouched by these settings.

Live activation requires an authorized system-plugin install, selecting the new decoration library in kwinrc, and reconfiguring KWin. The original packaged plugin is preserved. Only the selected library changes; the selected theme is kept. Revert with:

```
kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library org.kde.kwin.aurorae.v2
qdbus6 org.kde.KWin /KWin org.kde.KWin.reconfigure
```

Before/after overview, deep and returned screenshots differ at only 44, 1 and 68 pixels respectively out of 4,953,600. A running nested compositor successfully switches from the original to the patched plugin while retaining all 25 windows. Native decoration movement/release, resize, scaled input, urgency, closing and real-frame damage fixtures were checked. The old camera-coverage fixture assumed undecorated origins; it failed identically on the original theme and was corrected to use the client offset, then passes on both engines. Decoration and edge-resize fixtures now have the task-owned socket settings required to reach their assertions. All 20 CTests pass. No full suite, commit or push.

Ivan authorized installation once validation completes. Activation and exact selected ID are recorded below after installation.


Activated after Ivan's explicit authorization: `org.kde.kwin.aurorae.krellis_1030123148`. KWin reports that plugin with the unchanged `__aurorae__svg__FieldworkDark` theme. The installed binary matches the staged SHA256 `a5deab13c18ba3f7bfe887fd8f3c1c148465b5ae6b74833a5b9672aaedc480ec`. The original plugin is intact. Krellis remains `krellis_1030115620`, running/installed stamp `build Oct 30 2026 11:56:25`. Activation is verified, and Ivan reports improved performance on the live desktop. He requested committing and pushing all current project changes; final precommit validation is in progress.
