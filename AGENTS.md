# kwin-trellis

A port of DanFessler/trellis (infinite-canvas "desktop": camera over a tiled layout, springs, docking, minimap) to KDE Plasma 6 / KWin Wayland. Clean-room: follow the semantics of the Trellis TypeScript, do not copy its code (noncommercial license). Reference sources are read-only at `~/dev/trellis_kde/trellis/packages/core/src/`.

## Layout

- `core/`: pure C++20 model library `trellis_core` (no Qt GUI, no KWin). tree, document, spatial (focus layout, framing, docking targets), resize (push drags), motion (springs, tweens, easing), builder, navigator (camera state machine).
- `tests/`: QtTest, one executable per core module. Ported from the Trellis tests where they exist.
- `spikes/hello/`: minimal out-of-tree KWin effect used to prove the toolchain.
- `tools/nested.sh`, `tools/shot`: nested-KWin test harness and screenshots.
- `third_party/`: ECM, Vulkan headers and the KWin 6.7 source (gitignored, local workaround for missing dev packages).

## Build and test

Core only (fast, no KWin needed):

    cmake -S . -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-core && ctest --test-dir build-core --output-on-failure

KWin effect (needs `PATH=$HOME/.local/bin:$PATH`):

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$PWD/third_party/ecm-prefix \
      -DVulkan_INCLUDE_DIR=$PWD/third_party/vulkan-headers/include

## Testing effects safely

Never load experimental effects into the live session. Use the nested compositor:

    tools/nested.sh <plugin-root> <effect-id,...> <scenario-script> [width height]

Set `NEST_BACKEND=virtual` to render off-screen: no window opens on Ivan's desktop, so automated runs cannot steal focus (verified with `basic.sh`; always use this for unattended tests, and a distinct `NEST_SOCKET`/`NEST_WORK` when his preview is running). Only `tools/preview.sh` should use the default windowed backend.

It runs its own D-Bus session and throwaway `XDG_CONFIG_HOME`, socket `trellis-nest`, work dir `/tmp/trellis-nest`. Screenshots inside it: `spectacle -b -f -n -o`.

## KWin facts (6.7.5)

- Effects use the 3-arg `prePaintWindow` and `paintWindow` with `WindowPaintData`; `isActive()` must return true.
- `Window::inputTransformation()` is translate-only; scaled input needs a custom `InputEventFilter`.
- Out-of-tree effects are ABI-locked to the exact KWin version; rebuild after KWin upgrades.
- Qt Widgets must be in `find_package` for `KWin::kwin`.
- Verified in the nested KWin: `data += QPointF` / `data *= QVector2D` scale from the window's top-left; off-screen `moveResize` is honoured; D-Bus interface is `org.kde.kwin.KWin.TrellisEffect` on `/Trellis`.
- New windows append at the root's end seam (`seamTarget` + `applyDockTarget`) so columns share 1/n equally.

## Gotchas

- Lambdas that outlive their scope must capture `axis` etc. by value (a dangling capture broke `dragBoundary`).
- `DragHost::lastRects()` must return a stable snapshot (`m_lastRects`, rebuilt by `refreshRects()` at frame boundaries): `DragSession`'s ctor holds iterators across calls, and a map rebuilt per call is UB (wrong lifted size).
- Model `Node`s are immutable (`shared_ptr<const Node>`); every edit returns a new tree.

## Current implementation

The ordered implementation list received implementations, but full desktop-demo parity is not complete; see `HANDOFF.md` for verification and remaining hardware limitations. `TrellisEffect` coordinates independent `TrellisCanvas` instances per output. `effect/surface_input.cpp` implements scaled Wayland input and popup transforms; `effect/view_rules.cpp`, `bar.qml`, and `tiles.qml` implement rules, framing names and small icon tiles. `effect/persistence.cpp` and `session_store.cpp` provide session storage backed by pure model rebinding in `core/session.cpp`.

Animations: camera spring 210/29; layout 460 ms easeOutQuint; appear 220 ms; dock hide 300 ms; restore 380 ms; close 180 ms. Keep every visible transition animated.

## Rules

Follow `~/AGENTS.md`: no pushes, no commits unless asked, no secrets.

- Never `pkill -f <pattern>` from a tool command: the pattern matches the shell itself (exit 144). Use `pkill alacritty` or exact names.
- Never call `Navigator::endGesture()` from the frame loop: a pinch/drag only ends via `finishGesture()` (an early `endGesture` made `finishGesture` a no-op).

## Work area (2026-10-20)

- For each output, use KWin `clientArea(MaximizeArea, output)` in logical coordinates; reserve the Trellis bar inside that rectangle. Do not use global `WorkArea`: it can combine unrelated output struts.
- Poll client areas every 100 ms because panel strut updates need not change output geometry. Window geometry and bar/minimap placement glide for 460 ms with easeOutQuint, including updates during an existing flight.
- `scenarios/area.sh` builds `scenarios/panel.cpp` against installed Qt6Gui and LayerShellQtInterface and tests real top/bottom layer-shell exclusive zones and removal. Run only in the nested harness.
- Reject `LayerShellV1Window` explicitly when managing windows: layer-shell surfaces can report a normal window type and otherwise become tiles. Use QObject `inherits`: this internal subclass does not export `staticMetaObject`, so `qobject_cast` makes the effect fail to load.
- Ivan chose independent canvases. `effect/multicanvas.cpp` owns the effect coordinator; the existing behavior in `trellis_effect.cpp` is now `TrellisCanvas` (QObject + NavigationHost + DragHost). Only the coordinator is an effect-chain participant and input filter owner. Canvas methods must not forward pre/post-paint down the chain; the coordinator does so once. Window painting is dispatched to one owner.
- `Window::moveResize` changes its reported output when a camera places a neighbour across an output edge. Guard these writes with `m_applyingRest`; do not interpret them as user transfers. Explicit KWin output changes are deferred and transferred, while drag transfer uses the release pointer.
- Paint regions are DEVICE coordinates in this KWin version. Clip with `RenderViewport::mapToDeviceCoordinatesAligned`, not raw logical output geometry. Transfer flights may cross outputs; ordinary canvas windows may not.
- `NEST_OUTPUTS=2 tools/nested.sh ... scenarios/monitors.sh 1280 800` starts two outputs. Scenario control adds `selectOutput`, `dumpOutput`, `moveToOutput`; window indices are local to the selected canvas. Output disable migrates windows; reconnect starts an empty canvas.
- Nested Wayland output scale changes are deliberately ignored by KWin. Rotation is supported and tested; fractional output scaling is still unverified. No live-session testing has been performed.

## Scaled input and popups

- `effect/surface_input.cpp` maps the painted frame back through the resting frame and then `Window::inputTransformation()` into Wayland surface coordinates. Do not just scale event x/y: frame origins, client/buffer offsets, popup offsets and output origins all matter.
- Pointer hit-testing uses the visual stacking order and output ownership, not KWin's resting geometry. A client grab owns subsequent motion/releases, including outside the output; gestures/UI release focus and block app events. Keep press/release pairs balanced, including popup-dismissal clicks. Physical move/resize must pass through: KWin's InteractiveMoveResize filter is AFTER Effects.
- Popups never enter a document. Their painting and input inherit the owning parent's transform recursively; they use 220 ms appear and 180 ms close. `visualRect` is shared by paint and input, and closed popup refs live in `m_closing`.
- `scenarios/scaled.sh` builds `input-client.cpp` with installed Qt6Widgets and inspects actual Wayland deliveries. Its coordinate tolerance is 0.3 px because `dump` prints normal window rectangles to 0.1 px. It asserts the test is genuinely scaled and that the popup is scaled too. `core/input_geometry.h` has independent inverse/child-rectangle tests.
- Real hardware routing, decoration drags and pointer constraints remain unverified. Never load into the live compositor to check them.

## View policies and saved layouts

- Save opens the bar's editable framing name. QML receives key events through `OffscreenQuickScene::forwardKeyEvent`; suppress paired client releases after Return/Escape closes it.
- Rules use Qt INI nested keys in `$XDG_CONFIG_HOME/trellisrc`: `[ViewRules]` followed by `app-id\placement=stage`, `app-id\scaling=inert`, `app-id\minWidth=800`, etc. Placement is side/stage/floating; scaling is interactive/inert/none. Permissions: allowSide, allowStage, allowFloating; singleton and closable are booleans. Reload via D-Bus `reloadRules`. Invalid files must not partially apply.
- `toggleDock` (Meta+Shift+D) docks a float beside the stage or restores its remembered viewport-normalized floating rectangle. Respect placement permissions. Tiny panels show native app icons; their opacity uses spring 210/29 and visual stacking must prevent input through covering windows.
- Persistence is schema 1 JSON in `$XDG_STATE_HOME/kwin-trellis/session.json` (QStandardPaths GenericStateLocation), atomically written with owner-only permissions. Store structure, ratios, floats, hidden restore anchors and navigation per output. Never serialize window titles or process IDs.
- Ivan chose unique matches only: identify windows by desktop app ID/resource class plus role, and restore only if the identity occurs exactly once among both saved and live windows. Leave duplicates fresh. Match globally across outputs; output identity uses manufacturer/model/serial with connector fallback.
- Startup restores after a 750 ms quiet window and may retry late apps until user interaction. Pending unique matches block automatic overwrite. Corrupt/unsupported state stays untouched and disables writing. Preserve the last nonempty state during logout teardown.
- Saved structural IDs can collide with fresh panel IDs. `rebindSession` renames colliding structures and remaps navigation/hidden anchors, preserving noncolliding structures. Restore anchors may refer to splits, not just stages/panels.
- Harness isolates XDG_STATE_HOME. `NEST_SESSION_FILE` seeds state before startup. Persistence scenarios run save then restore with the saved `out/session.json`; `persist-monitors.sh` runs twice with NEST_OUTPUTS=2. Never seed from Ivan's personal state.

- Hidden panels have no layout rectangle. `applyRest` must skip model-hidden panels, including before native minimize during session restoration. Moving them to an empty rest rect produces a later Wayland configure commit that changes native output ownership after the synchronous geometry guard ends. Two-output persistence tests cover this regression.

## Audit corrections

Read the desktop parity audit in HANDOFF.md before claiming completion/readiness. Stage-relative floating, the original launcher/magnifying dock, collapsed-group detail and reduced motion remain gaps. Lifecycle, interrupted flights and native resize now have implementations and nested coverage; see the latest HANDOFF.md continuation notes. No install rule/package exists. Do not infer subjective polish or full parity from scenario exit codes or matching timing constants.

- Nested native-state fixtures use KWin scripting with `console.warn` and the harness forces Qt stderr logging, keeping snapshots in its own `out/kwin.log`. Do not inspect the live session journal for test output. Set `WAYLAND_DISPLAY=$NEST_SOCKET` inside the scenario; the incoming display belongs to the outer compositor.
- Before recording fixture geometry, wait for native Wayland configure commits, then minimize in a separate script turn. A requested rectangle and committed frame can differ while a client is hidden.

## Native resize and interrupted flights (2026-10-20)

- Replacement flights must capture `visualRect` plus opacity before mutating the model or dropping an existing flight. Close holds the deleted window and minimize-visible reference until its captured fade completes. `interrupt.sh` covers reversals and close during glide/hide.
- `native_resize.cpp` handles real KWin resize signals. Pointer deltas are logical screen distances, applied to the initial painted edges; do not multiply by the existing paint scale. Native requested geometry can jump to a client minimum and is unsuitable as the visual delta.
- Resize preview pins the camera; split edits use `dragBoundary` and floating edits use normalized geometry. Session writing skips active previews. Cancel restores the saved document, accept remembers floating geometry, unload cancels native grabs before baseline restoration.
- `resize.sh` checks actual native stepped signals and commit/cancel, zoomed boundary deltas, scaled floats, opposite corners and rule-reload cancellation. `resize-stage.sh` repeats it with a stage policy; a seeded root split containing a stage-wrapped panel additionally covers transparent ancestor traversal. The new lifecycle branch checks unload during native resize. None establishes physical input feel or full desktop parity.
- `tools/preview.sh` provides an isolated interactive desktop with temporary state. Never use it concurrently with another nested harness (same socket). `TRELLIS_PREVIEW_SMOKE=1` exits after producing a screenshot.

## Rendering and native decoration input (2026-10-20)

- Always set screen-level `PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS` before KWin prepares scene damage. Per-window `setTransformed()` alone leaves partial surface damage in native coordinates. Current implementation chooses full repaint for correctness.
- `QHoverEvent(type, scenePos, globalPos, oldPos)` takes LOCAL scenePos first. Keep a painted-decoration hover target, refresh hover on press, and synchronize `input()->pointer()->decoration()` to the painted hit. Wrong coordinates change `mouseGravity()` and turn title centres into resize corners.
- A decoration grab must receive its release even during a native move/resize and must bypass its own canvas drag-inertness. Otherwise the move stays active and the following click can target the previous window. Use native interactive anchors for move start/step.
- `decorations.sh` uses a real Wayland fake-input fixture restricted to task-owned `trellis-test-*` sockets. Build `trellis_paint_probe` explicitly for real-frame pixel checks; the probe never requests repaints. Scoped `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` is needed only for this nested test, never change the live compositor.
- Screenshot capture renders a new scene and cannot establish idle partial-repaint correctness. The paint probe checks real frame buffers, including output/texture transforms.
- `tools/nested.sh` honors an optional `NEST_SOCKET` and refuses occupied sockets. Keep `NEST_WORK` distinct per run. Atomically stage plugins to avoid truncating a mapped binary during a rebuild.

## Installing and trying it on the live desktop (2026-10-20)

- `tools/install.sh` builds in `build-live/` and runs `sudo cmake --install --prefix /usr`, putting `trellis_<stamp>.so` in `/usr/lib/qt6/plugins/kwin/effects/plugins/` (older `trellis*.so` are removed). Every install uses a fresh plugin id (`-DTRELLIS_ID`; also the .so name and the `qrc:/<id>/` prefix) because KWin never re-reads a plugin file it has already mapped, so reusing `trellis.so` kept the old code running until a logout. `tools/live.sh on` unloads whatever Trellis is loaded and loads the newest installed id; `status` compares the running `version` D-Bus stamp with the installed file. The harness keeps the default id `trellis`. KWin only searches the system Qt plugin path (`/usr/lib/qt6/plugins`); a `~/.local` install is not found. ECM's own plugin dir resolves to `lib/plugins` here, so `effect/CMakeLists.txt` uses `TRELLIS_PLUGIN_DIR` (`lib/qt6/plugins`). The installed library has no RPATH.
- Installing does not enable it (`EnabledByDefault` false). `tools/live.sh on|off|status` loads/unloads it through `/Effects` and writes nothing to kwinrc, so a logout or KWin restart clears it. `tools/uninstall.sh` removes the file.
- Emergency exit: global shortcut Meta+Alt+Escape ("Trellis: Turn Off", D-Bus `turnOff`) unloads the effect; `scenarios/turnoff.sh` shows windows return to native geometry. The ABI is locked to KWin 6.7.5, so reinstall after every KWin upgrade, and never enable the effect permanently across an upgrade without rebuilding.
- Do not run `install.sh`, `live.sh on` or any `sudo` from an agent session unprompted; Ivan runs them (`! tools/install.sh`). A crash of the effect would take down every Wayland app, including the agent's terminal.

## Bar, shortcuts and drag sizing (2026-10-20)

- The bottom bar is **off by default** (Plasma's panel covers most of it) and reserves no space unless on. Meta+Alt+B toggles it (`toggleBar`), Meta+Alt+M the minimap (`toggleMap`), Meta+Alt+S starts saving a named view (`saveView`; the bar floats over the windows just while naming). Scenarios that need the bar (`bar`, `area`, `map`, `names`) call `toggleBar` first; `bar-optional.sh` covers the default.
- Lifting a window for a drag shares its space among all siblings in proportion (`removePanel(..., keepProportions)`); closing/hiding keeps the upstream "next sibling absorbs it" rule. Dropping back into the same shape restores the original weights exactly (`sameShape` in `core/drag.cpp`). `scenarios/redrop.sh` guards this. vibetiles also tiles windows; unload it (`qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript vibetiles89`) before trying Trellis live.
- `lifecycle.sh` needs socket `trellis-nest`. Always set `NEST_BACKEND=virtual` for unattended runs; the default backend opens a window on the desktop.

## Edge resize and real drag preview (2026-10-20)

- Real edge presses on tiled windows did nothing: KWin extends a decorated window's input region outward by `decoration()->resizeOnlyBorders()` (into the tile gap), but `TrellisEffect::surfaceHit` only tested the frame. It now does a second pass over windows with their resize-only borders added (only when no window body is under the pointer). Scenario `edge-resize.sh` (real pointer events) asserts it.
- During a drag, `applyRest()` no longer returns early: while a session previews a drop, every window except the lifted one is `moveResize`d to its previewed target (`restRect(panel, world)`), so clients redraw at the real size instead of being stretched. Cancel/commit restores via the normal rest layout. Scenario `previewsize.sh`.
- `decorations.sh` needs both effects: `nested.sh build/root trellis,trellis_paint_probe ...`.
- Round-trip "drag under another window and back" is width-stable in the model (`roundTripsUnderAndBackKeepWidths` in `tests/test_drag.cpp`, 12 variants x 4 cycles, equal starting weights). The shrinking-left-window report is NOT reproduced yet; suspects: unequal weights, min-size handling, or interference from the vibetiles KWin script.

## No stretched contents (2026-10-20)

Ivan's "left window keeps shrinking" and the ugly drag preview were the same thing: `paintWindow` scales by `shown/frame` per axis, so any window drawn at a rect with a different aspect than its frame (tweens, the drag card, previews while clients lag) had stretched glyphs. Sizing in the model was right all along. `TrellisCanvas::visualRect` now returns the largest aspect-preserving rect (top-left anchored) in the normal unzoomed view; it deliberately keeps per-axis fitting for zoomed views, live divider drags, floats and clamped (min/max rule) windows. `dump` rows end with `drawn WxH`; `previewsize.sh` asserts drawn aspect == frame aspect during a drag.

## Real-size animation and native maximize (2026-10-20)

- While `m_tween` runs, `prePaintScreen` calls `applyRest(true)` every tick so clients really resize through the animation (frame size = tweened rect size); one final `applyRest()` when it ends. The aspect-fit in `visualRect` then has nothing to correct. The earlier version jumped the frame to its final size and shrank the drawn rect to that aspect, so windows popped small at the top right and grew back. `scenarios/jank.sh` guards it.
- A native maximize (title-bar double click, maximize button) on a tiled, non-float window is undone and turned into `toggleMaximize` for its panel (deferred with a zero timer). `scenarios/maximize.sh`.

## Two monitors, maximize and cross-output drags (2026-10-20)

Ivan runs two outputs (DP-2 3440x1440 landscape, HDMI-A-1 1440x2560 portrait to its right), so multi-output paths are the live path, not an edge case.

- Zooming a canvas onto one panel (panel maximize) makes `applyRest` place every other tile outside its output, and KWin then reports `outputChanged` for windows that now sit on the neighbour. `TrellisCanvas::placedByUs(w)` (each `Tracked` remembers the frame `applyRest` last asked for) lets `watchOutput` ignore those changes, both at signal time and again in the deferred handler. Only a change we did not cause transfers a window. `scenarios/maximize2.sh` (`NEST_OUTPUTS=2`) maximizes and restores and asserts every window stays on output 0.
- A KWin-driven title-bar drag that leaves its output is handed live to the canvas under the pointer: `TrellisCanvas::m_handoff` (set in `syncOutputs`) cancels the source session, `transfer`s the window, and `adoptDrag` starts a fresh session on the destination, grabbing the new tile by the middle of its title bar so the lifted card follows the pointer. The destination shows the drop slot and real-size reflow preview, and the drop commits there; Escape still counts as a cancel because the original start geometry is carried over. `scenarios/cross-drag.sh` drives it with real pointer events.
- `monitors.sh` ends with kscreen-doctor rotate/hotplug steps addressed to `WL-1`, which exists only with the windowed nested backend; under `NEST_BACKEND=virtual` it stops there (everything before passes). `monitors.sh` and `persist-monitors.sh` now call `toggleBar` first because the bar is off by default (one global toggle, not per output).

## Floats versus the desktop example (2026-10-20)

Re-reading `examples/desktop/src` (App.tsx, desktop.ts, AppWindow.tsx): the demo mounts `<Workspace floating="stage">` with an empty `<Stage>` as the desktop, every app type has `placement="float"`, and `launch` opens windows as stage floats cascaded over the desktop. Tiling is the secondary mode: dragging a window past the stage edge docks it beside the desktop, the green button / "Dock Beside Desktop" calls `toggleDock`, and "Maximize" (frame the panel) is offered only for docked windows. Floats are stage-relative (they follow the camera and zoom with the desktop).

The effect only has viewport-fixed Overlay floats (`floatingLayer()` returns Overlay, `floatWorld` returns nothing). A tiled window can therefore never be dragged into a float; only Meta+Shift+D or a `placement=floating` rule makes one, and an existing float re-docks when released within the overlay edge band. Matching the demo means a Stage-as-desktop mode: `floatingLayer()` = Stage, an empty Stage root, `floatWorld`/`floatScreen` for stage floats, new windows opening as stage floats, and stage floats in persistence, resize and painting. Not built yet; see the open decision in HANDOFF.md.

## Shift-drag floats (2026-10-20)

First float affordance (option 2; the Stage-as-desktop mode is option 1 and will live alongside it): holding **Shift** while dragging a window by its title bar floats it where the card is dropped, and stops an existing float from docking at the screen edge. `DragSession::setForceFloat` (core) overrides the target with Float while the pointer is on screen; `TrellisCanvas::moveDrag` feeds it from `m_shiftHeld`, updated from every pointer motion event (`PointerMotionEvent::modifiers`) in `TrellisEffect::handleMotion`, so the modifier is re-read on each step, including after a cross-output handoff. Shift pressed without moving takes effect on the next motion. `simForceFloat(1|0|-1)` D-Bus slot stands in for the key in tests. `scenarios/float-drag.sh`.

## Float edge and corner snap (2026-10-20)

Ported from vibetiles' `edgeDropTargetRect` (`~/dev/trellis_kde/vibetiles/kwinscript/contents/ui/main.qml`). `DragSession::snapZone()` (core/drag.cpp): when the drop target is an **overlay Float** and the pointer is within `SnapEdgePx` (24) of a viewport edge, the float lands as that half; with one horizontal and one vertical side within `SnapCornerPx` (120) it lands as that quarter. `floatRect` returns the zone, and `updateSlot` draws it as the translucent slot (dump line `SNAP x,y wxh`). Only reachable while the target is Float: with Shift held, or an existing float moved over empty space away from the outer frame band. Without Shift, the viewport edge is the original's outer **frame band** (`frameDropTarget`), which docks the window across the whole edge of the layout; that already is the edge-drop for tiles. `scenarios/float-snap.sh`.

Obstacle fill (added later, from vibetiles): floats under the zone are not covered when there is room. `clearOf` shrinks the zone around them, keeping the piece that holds the pointer; an edge may grow past its half toward the screen, a corner stays inside its quarter. If the result is under half the zone in either direction (a sliver) the plain half or quarter wins and the floats are overlapped. `scenarios/float-fill.sh`.

## Floating-first / tiling-first modes (2026-10-20)

`[General] floatFirst` in `~/.config/trellisrc` (default false = tiling-first). Toggle: Meta+Alt+F, D-Bus `toggleFloatFirst`, `setFloatFirst(bool)`, `floatFirst()`; the setter persists to trellisrc and updates every canvas (`TrellisEffect::m_floatFirst`, mirrored into each `TrellisCanvas`, loaded at the top of the `TrellisEffect` ctor).
- New windows: a per-app `[ViewRules]` `placement` wins (`ViewRules::placementSet`); otherwise float-first opens a cascaded float (`0.2+0.025k, 0.15+0.035k, 0.6x0.65`, k = floats % 6) and tiling-first keeps the old Side placement.
- Drag drop (`DragSession::setFloatFirst`, end of `retarget()`): in float-first the drop is Float unless `edgeDock()` finds a tile target: pointer within `DockEdgePx` (24) of a viewport edge and at least one tile exists -> `frameDropTarget` (docks across the whole layout edge); within `DockCornerPx` (120) of the perpendicular edge as well -> `tileDropTarget` on the corner leaf (a quarter-ish split). With no tiles nothing docks; the float snap zone (half/quarter, above) applies instead. **Shift inverts the mode**: Shift in float-first gives the normal tiling resolution, Shift in tiling-first floats (and snaps).
- Overlay floats are confined to the usable area (fix for floats drawn over the Plasma panel; `setKeepAbove` puts them over the Dock layer): `floatRect` clamps, the live card is clamped, and `clampFloat(rect, layer)` (document.cpp) clamps overlay floats on insert, resize (`native_resize.cpp`), session restore and sanitising. Stage floats keep the loose `clampFloat(rect)`.
- Overlay float tab targets are skipped in hosts without tabs (`tabsAllowed()`); before this, dropping a float onto another float found no target and snapped back. `scenarios/float-over-float.sh`.
- Tests: `scenarios/float-first.sh`, `float-fill.sh`, `float-over-float.sh`.

## No content blink during glides (2026-10-20)

A client must never be asked for its FINAL size while the drawn rect is still animating: its contents lay out at the final size and are squashed into the shrinking or growing rect for a frame (the "blink"). Rules now: `applyRest()` always follows `m_tween` (layout glides: maximize, dock, drop, rule change, hide/restore) so requests equal the drawn size; `restRect` and `visualRect` use `sizingCamera()` (the animated camera while it glides, the target otherwise; target during a drag session or native resize); `prePaintScreen` re-requests each frame while tweening or springing and once more at rest (`m_steppingResize`). `endDrag` begins the tween BEFORE `relayout(false)` so the first request is already the drawn size. Scenarios: `zoom-blink.sh` (Ctrl+wheel) and `maximize-blink.sh` sample frame vs drawn width.

**Throttle (2026-10-20, late).** Following the drawn size every frame made heavy clients (Brave) re-lay-out at dozens of intermediate sizes, which felt stuttery next to a manual border drag. While a glide runs (`applyRest(true)` from `prePaintScreen`), a client is given its next size only once it has landed the previous request (frame within 1px of `Tracked::requested`) or has sat on it for `SteppingPatienceMs` (120). The final `applyRest()` at rest is unconditional. Not tuned against a real Brave yet.

## Client-side-decorated edges (2026-10-20)

Brave/GTK-style CSD windows keep their resize band in the shadow their surface draws outside the frame. The second `surfaceHit` pass (no window body under the pointer) now widens `reach` by `bufferGeometry` minus `frameGeometry` for undecorated windows (SSD still uses `resizeOnlyBorders`); the surface input region still decides via `hitTest`. Not testable in the nested harness (no CSD fixture); confirm with Brave live.

## Test environment notes

`NEST_BACKEND=virtual` has no pointer device, so Qt clients never get `wl_pointer`: `rules.sh`, `scaled.sh` and `decorations.sh` (which assert client CLICK lines) only pass on the Wayland nested backend. `lifecycle.sh` needs `NEST_SOCKET=trellis-nest`. `persist-restore.sh` needs `NEST_SESSION_FILE=<persist-save out/session.json>`; `persist-corrupt.sh` needs a `{"schema":99}` seed. `persist-save.sh` toggles the bar first (off by default).

**Glide switch (2026-10-21).** `[General] glide=false` in `~/.config/trellisrc` (default true) turns off the camera spring and the layout tween: `reducedMotion()` makes `Navigator::retarget` finish the camera at once, and the three `m_tween.begin` sites are skipped. Windows then jump to their final rects, one real size request per action. Added because the spring still felt stuttery with the throttle. Pickup/drop session animations are untouched. Tests: `NEST_TRELLISRC=<file>` seeds `trellisrc` in `tools/nested.sh`; zoom-blink, maximize-blink, drag, resize, anim pass with glide off.

**Glide correction (2026-10-21, later).** The first `glide` switch never took effect live: Qt's INI reader files `[General]` keys under bare names, so `value("General/glide")` always returned the default (same latent bug for `floatFirst`, now fixed; writes via `setValue("General/…")` are fine). Read bare keys. `glide` now defaults to **false** and, when off, also disables flights (duration 1 ms, bookkeeping kept), appear, popup and close animations, besides the camera spring and layout tween. `tools/nested.sh` writes `glide=true` into the test `trellisrc` unless `NEST_TRELLISRC` is given, so animation scenarios still test the glide paths. Lesson: a passing nested test with a config key proves nothing unless the log shows the key was read.

**No-glide landing (2026-10-21, last).** With `glide=false` the new layout is final on the first frame while clients still hold old buffers. Stretching them is the content blink; drawing them at the new top-left with their old size opens gaps (desktop visible when zooming in). So `visualRect` holds every window at its real `frameGeometry` while any tracked window `awaitingLanding` (frame differs from `Tracked::requested` by >1px, within `LandingPatienceMs` = 300 of the request; rule-clamped, hidden and flying windows excluded), then the whole new layout appears at once. During a native border drag only the lagging neighbours are held, not the dragged window. `requestedMs` only moves when the target changes. Test: `scenarios/noglide-blink.sh` + `scenarios/slow-client.cpp` (250 ms layout per resize), with `NEST_TRELLISRC` = `glide=false`; asserts every drawn rect is exactly the frame or exactly the final rect. Unverified live.

## Landing animation (2026-10-21, last)

With `glide=false` (default) a window whose real `frameGeometry` changes plays a short move-and-scale from its old frame to the new rect (`TrellisCanvas::frameChanged`, `m_landings`, easeOutQuint). The content is already laid out at the final size, so it cannot re-introduce the blink. `[General] landMs` sets the duration (default 200, `0` = instant). It is skipped during drag, edge-resize, flights, user move/resize, minimized windows. Scenario: `scenarios/land-anim.sh` (run with a `glide=false` rc). Ivan said the blink is gone before this was added; the animation itself is unconfirmed on the live desktop.

Geometry Change (scripted effect) was instant alongside Trellis; the cause was not pinned down. Likely our `paintWindow` rewrites the translate/scale for every window, so its transform is overridden or fights with our rect. If both are enabled, expect doubled animation; turn Geometry Change off for Trellis windows or set `landMs=0`.

Fix (2026-10-21, later): the no-glide hold must NOT apply during a live border drag (`m_resize`). A slow client (Brave) next to the dragged window was held at its stale frame and trailed the edge; neighbours now follow the edge (slightly stretched) as before. The landing glide starts from `Tracked::lastDrawn`, so a window already shown at its final rect doesn't jump back when its client lands. Scenario `scenarios/resize-slow.sh` (slow-client neighbour; fails on the old behaviour).

Drop/hover animation (2026-10-21): `frameChanged` now also runs while a drag session is active, for every window except the dragged one, so neighbours glide as the preview reflows and when the drop lands (the dragged window itself already glided via lastDrawn). Side effect: a ~200 ms scale of the content while a neighbour changes size, so `scenarios/previewsize.sh` must run with `landMs=0`. `scenarios/drop-anim.sh` covers it. `drag-damage.sh` needs the paint-probe effect and was not run.

## Grid zoom, title-bar scroll, minimap help (2026-10-21, last)

- **Wheel zoom is one grid step per gesture.** `Navigator::wheelStep` gates on a 180 ms quiet gap (or a direction change) using a WALL clock passed by `TrellisCanvas::handleAxis`; `m_nowMs` only advances with paints, so it is stale between gestures and must not be used for this. `zoomStep(out, worldPoint)`: out grows the framed box by the neighbouring column and row on the side away from the pointer, in grows toward the pointer (the tile under it must stay inside), both axes at once; leaf sets are closed over tiles the new box cuts. A box that is not a tree node ("boxed", e.g. two columns of a two-row grid) lives in `m_boxed`/`m_framedLeaves`, survives `sync()`, and `overviewing()` (not `framed().empty()`) means "showing everything".
- **Inputs:** Alt or Meta + notched wheel, Ctrl+Alt + any scroll source, or a plain scroll over a server-side title bar (`TrellisEffect::handleAxis` computes `titleBar` via `surfaceHit`: decoration, not inert, same canvas, above `clientGeometry().top()`, no client grab). Plain Ctrl+scroll is NOT ours any more (apps zoom contents with it). CSD windows have no title bar to target.
- **Minimap:** header shows a Floating/Tiling pill (`floatFirst` property, refreshed from `setFloatFirst`) and a (?) whose hover (`hitKey: "map:help"`) opens the shortcut sheet. The scene is always sized for the sheet (304 x cardH+helpH): resizing a live `OffscreenQuickScene` did NOT re-render it (the old texture got stretched), so the QML exports `inputRect` and `sceneContains` only takes input there. Clicking an already framed tile (`map:frame:<id>`) steps out instead of re-framing.
- Scenarios: `zoom-grid.sh` (Alt/Meta/title-bar/Ctrl, mode pill, help sheet), `map.sh` (click framed tile zooms out). `scaled.sh` fails on the baseline too (portal "App info not found" noise in the fixture's client log).

## Minimap drag-select and mode pill (2026-10-21, last)

- Pressing on the map stage (`map:overview`, `map:frame:*`, `map:focus:*`) and moving 5 px starts a square (`m_mapDragging`, `moveMapSelect`/`finishMapSelect`); release frames the touched tree tiles via `Navigator::frameTiles` (-> `frameSet`, boxed when they are no node). Tiles count when the square reaches their drawn (gap-inset) rect; floats are ignored. The QML exports `stageRect` (root coords) and takes `band`/`bandOn`/`picks`; Esc cancels. A press without movement is still the old click action.
- The Tiling/Floating pill has `hitKey: "map:mode"`; `barAction` calls `TrellisCanvas::m_toggleMode`, set by `TrellisEffect` to `toggleFloatFirst` (all canvases + trellisrc).
- Scenario `map-select.sh`; unit test `frameTilesFramesExactlyThose`.

## Meta+drag square and auto-hiding minimap (2026-10-21, last)

- The zoom-to-rectangle gesture is **Meta+drag** (no Ctrl/Alt); Ctrl+Alt+drag still pans (`handleButton`, `square`). It used to be Ctrl+Alt+Shift, which collided with KWin's own Shift-drag tile overlay (Shift while dragging also drops windows into KWin custom tiles and resizes them; Ivan disabled that himself).
- The minimap auto-hides: QML `open = shown && (pinned || hot || bandOn || peek || linger)` drives `slide` (0 tucked, 1 out); the card is positioned from `slide` and is flush with the screen edge (`edge` = 12 px gap inside the scene). Tucked, only a slim tab (`map:tab`) takes input (`inputRect`); the strip between card and screen edge is a `map:card` item so resting on the tab spot keeps it open. `settled` also waits for the slide and the peek/linger timers, so the effect keeps painting.
- The pin (`map:pin`, header, next to the (?)) toggles `mapPinned` (`TrellisEffect::setMapPinned`, saved as `mapPinned` in trellisrc, read at load). The mode pill moved 22 px left to make room.
- Scenarios must hover the tab (`gx+gw-8, gy+gh-30`) before clicking the map once the 1.8 s peek is over (`map.sh`, `map-select.sh`, `zoom-grid.sh` do). The `MAP` dump line ends with `open <slide> pinned <0|1>`.
