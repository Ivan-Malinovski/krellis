# Krellis handoff

## Next agent: current state

The latest user report was that typing into terminal panels turned the terminal and Krellis bar black. A scoped workaround is in `tools/preview.sh`: it defaults `QT_QUICK_BACKEND=software` for the Qt Quick UI, while leaving the compositor and terminal clients on their normal renderer. A separate nested preview successfully typed and erased 100 characters and retained a visible terminal and bar. The GL allocation/framebuffer errors seen in the old preview did not recur in that run, but the underlying rendering fault is still unknown. Details and evidence are in **Typing black-out workaround** below.

Ivan was told to close the old preview and run `~/dev/krellis/tools/preview.sh` again. First follow up by asking whether typing still blacks out the terminal or bar. If it does, inspect the new preview's task-owned `out/kwin.log` and renderer path; do not assume the Qt Quick workaround fixed the underlying fault. Do not stop or replace the user's preview, and do not load the effect into the live compositor. The other recent user feedback about idle blinking and inaccurate title-bar drags has fixes and regression coverage documented in **Preview feedback: repaint and decoration input**.

Krellis (renamed from kwin-trellis on 2026-10-22; entries dated before that were rewritten mechanically, so they say Krellis, krellisrc and `/Krellis` where the code then said Trellis, trellisrc and `/Trellis`) is a port of DanFessler/trellis (the "desktop" demo: infinite canvas, camera over a tiled layout, springs, docking, minimap) to KDE Plasma 6 / KWin Wayland, as an out-of-tree KWin effect. Read `AGENTS.md` first (build, harness, gotchas, rules). `~/AGENTS.md` rules also apply.

Goal from Ivan: 1:1 port with the same sleek animations for everything. Clean-room: follow the semantics of the Trellis TypeScript at `~/dev/trellis_kde/trellis/packages/core/src/` and `examples/desktop/src/` (read-only), do not copy its code (noncommercial license). vibetiles code may be reused.

## State (2026-10-20)

The five requested implementation areas received implementations, but this does not establish full desktop-demo parity; see the source audit below. The continuation notes at the end supersede historical audit findings about lifecycle, interrupted flights and native resize. Core tests pass 10/10, the effect builds, and all 23 final nested scenario runs passed. Hardware-only limitations remain below.

- `core/`: immutable tree/document, layout/framing, resize, motion, navigation, drag, view policy and session rebinding.
- `effect/multicanvas.cpp`: coordinator with one independent `KrellisCanvas` per output, as Ivan requested. Each owns document, camera/history, named views, dock, minimap and gestures. Cross-output transfer glides; output removal migrates windows and reconnect creates an empty canvas.
- Per-output KWin MaximizeArea respects Plasma panel struts, polled every 100 ms. Reserve the 48 px Krellis bar inside this area; animate work-area changes for 460 ms.
- `effect/surface_input.cpp`: inverse painted geometry mapping for actual Wayland pointer motion/buttons/wheel, implicit capture and gesture inertness. Popups inherit parent transforms recursively and animate appearance/close.
- Bar Save opens editable Unicode name input; Return saves, Escape cancels, blank names stay open. View rules support placement/scaling/minimums/permissions/singleton/close command policy. Small panels show native icons with animated transitions and correct occlusion. `toggleDock`/Meta+Shift+D switches docking and remembered floating rectangles.
- Persistence: atomic schema-1 JSON under QStandardPaths GenericStateLocation (`$XDG_STATE_HOME/krellis/session.json`). Stores per-output layout/ratios/floats/hidden anchors/navigation/named views, without captions or PIDs. Ivan chose app ID + role matching only when unique in both saved and current sets; duplicates stay unmatched. Startup retries late apps until user interaction; pending unique matches prevent autosave overwrite. Corrupt/unsupported state stays untouched. Structural-ID collisions are renamed while remapping anchors.
- Test additions: input geometry, view policy, session codec/rebinding/identity, and further clean-room model semantics. Codex2 worker added final model/session coverage and collision regression tests on the second account; primary account runs graphical verification because its worker sandbox cannot bind a nested D-Bus socket.
- Animations: spring 210/29; layout 460 ms easeOutQuint; appear 220 ms; hide 300 ms; restore 380 ms; close 180 ms.

## Not done / open

- Real hardware: physical input routing, decoration drags, modifier mouse window actions, pointer constraints and real shortcut grabs remain unverified. Nested scenarios use D-Bus simulation of the same effect handlers and actual Wayland client delivery where supported. Never load into the live session to investigate.
- Fractional output scaling: nested Wayland backend ignores scale changes; rotation is supported and tested. Hardware scale behavior remains unverified.
- Tab synthesis is refused (`tabsAllowed()` false). Keymap/menu/gesture customization and further dock fade polish remain future work.
- Minimap camera-frame outline can blend into the focused tile border.

## Build, test, run

    # core
    cmake --build build-core && ctest --test-dir build-core --output-on-failure
    # effect (needs PATH)
    export PATH=$HOME/.local/bin:$PATH; cmake --build build --target krellis
    # scenario in the nested compositor (never load into the live session)
    (timeout 150 tools/nested.sh build/root krellis scenarios/<x>.sh 1280 800 >/tmp/nested-run.txt 2>&1)
    grep -v "^method\|^signal" /tmp/krellis-nest/out/log.txt
    # screenshots: /tmp/krellis-nest/out/*.png

Check `df -h /` before big builds (last: 86%, 17G free). The effect is ABI-locked to KWin 6.7.5; headers live in `third_party/kwin-src` (gitignored).

## Gotchas

- The project is not its own git repo (a parent repo reports `master`). No commits unless Ivan asks; never push; no git user is configured.
- Shell is fish. Scripts use `#!/usr/bin/env bash`. Never `pkill -f` from a tool command (kills the shell); use `pkill alacritty`.
- `DragHost::lastRects()` must return a stable snapshot (`m_lastRects`, `refreshRects()`).
- Never call `Navigator::endGesture()` from the frame loop.
- Model `Node`s are immutable; every edit returns a new tree.
- Scenario click coordinates must account for 1.5 px tile gaps and the stage area inside the minimap card.
- Never read or print secrets (`.env`, keystores, firebase json, `.ssh`, `~/.claude.json`). Don't drive Ivan's phone.

## Verification and continuation

Before persistence, all existing scenarios plus area, monitors, scaled, names, icons and rules passed; the final icon occlusion/input fix also passed its expanded scenario. Screenshots inspected include real exclusive-zone panels and floating windows covering icon tiles.

Final verification: all 23 runs exited 0: persist-save, persist-restore, persist-corrupt, two-output persist-monitors save/restore, names, icons, rules, scaled, area, monitors, basic, anim, many, hide, flights, input, drag, float, rect, bar, map and keys. Logs/screenshots are `/tmp/krellis-final2-<scenario>/out`, except monitors at `/tmp/krellis-final3-monitors/out`. The original batch was interrupted during monitors; its partial run is not counted. Core 10/10 was rerun by the primary account after the final effect fix. Builds succeeded. Scenario exits include smoke coverage; new scenarios add explicit assertions and actual client event logs.

The two-output persistence test caught a hidden window requesting empty native geometry and subsequently migrating to the first output on an asynchronous Wayland configure commit. `applyRest` now skips model-hidden panels; the final save/restore and monitors scenarios passed. Screenshot review covered restored independent canvases/named views, floating occlusion and Unicode framing names. No live effect loading, commits or pushes.

Persistence scenarios: run `persist-save`, then `persist-restore` with `NEST_SESSION_FILE` pointing to the save run's `out/session.json`. Run `persist-monitors` twice with `NEST_OUTPUTS=2`, similarly seeding the second run. `persist-corrupt` needs a task-owned seed `{"schema":999,"canvases":[]}`. Harness isolates config/data/cache/state; never use personal session state as a fixture.

Rules use Qt nested INI keys: `[ViewRules]` and `app-id\placement=stage`, `app-id\scaling=inert`, etc.; `[ViewRules/app-id]` is invalid Qt INI syntax. See `AGENTS.md` and scenarios/rules.sh. `closable=false` guards effect close commands; native client controls remain native. Singleton suppression is per canvas and minimizes duplicate managed windows.

## Desktop parity audit (2026-10-20)

Read-only comparison against local original `examples/desktop/src` and core runtime found broader gaps than the completed scenario list implied:

- Demo floats are stage-relative and camera-following; effect floats are viewport-fixed overlays. The demo supplies a desktop stage/backdrop by default, while the effect starts by tiling native windows.
- Original dock launches apps, cycles running windows, shows running indicators and magnifies icons. The effect bar only offers hidden-window chips; Plasma is still the native app launcher.
- Model push-resize exists, but the effect only starts drag sessions for native userMove, not native resize. No integrated split-boundary resize interaction.
- Tabs, collapsed nested-group detail, configurable panel menus and reduced-motion behavior are absent or incomplete. Native KWin menus do not implement the demo's custom panel menu.
- Main spring/timing constants match, but peripheral UI behavior differs: chip/framing removals have no exit transition; dock magnification and reduced-motion handling are absent. Timing equality is not a subjective smoothness/performance certification.
- No CMake install rule/package. Build stages `build/root/kwin/effects/plugins/krellis.so` for the harness. This machine's Qt plugin root is `/usr/lib/qt6/plugins` and installed KWin is 6.7.5-1.1. Do not load live for testing.
- Effect unload does not restore original native geometry or keep-above state; canvas destructor is default. A safe production enable/disable lifecycle remains required.

The 10/10 tests and 23 scenario runs remain valid evidence of the covered behavior, not of complete parity or production readiness. User asked for this audit and usage/install instructions; no live installation was performed.

Additional source-confirmed motion gaps from the Codex2 read-only audit, cross-checked by the primary agent:

- Original layout commits capture actual screen rectangles (`lastRects`) and tween toward screen targets. The effect interpolates layout/world rectangles before camera projection, producing different trajectories when camera and layout move together.
- Hide/toggleDock capture `screenRect`, omitting active flight transforms; close erases its flight before capturing the starting rect. Interrupted actions can jump. Fix by capturing the currently painted rect/opacity before replacing any flight.
- Pan/pinch snap-candidate API exists in core but has no effect-side preview; rectangle selection has its own overlay. Native floating resize also never commits updated normalized geometry.

## Away-work continuation

Ivan asked what can be worked on while away. Active scope: (1) preserve and restore native window state across effect unload/reload, (2) continuity when flights are interrupted, (3) native floating/docked resize integration. Codex2 worker owns implementation/tests/scenario files and builds; primary agent reviews and runs the nested harness, then updates these notes. Phase gates require the new scenario plus existing basic/anim/many/hide/flights/input/drag/float/rect/bar/map/keys and core tests before moving to the next item. No live install, session loading or physical-device testing is authorized by this continuation.

Away-work phase 1 intermediate verification: native lifecycle scenario passed on one and two outputs, and basic/anim/many/hide/flights/input/drag/float/rect/bar/map/keys all passed under `/tmp/krellis-away1-<scenario>/out`; core 10/10. Window state snapshots are global, initial minimized windows enter the hidden model before layout, and hide restores original native size while a visible-ref flight keeps the client rendering. Direct immediate unload before a Wayland client commits remains best-effort. Final phase-1 check is pending the current-output hidden-target correction, transferred-then-hidden coverage, and scoped legacy scenario cleanup.


## Continuation from session history (2026-10-20)

Resumed the primary session in `/home/ivan` and its implementation worker history. No worker was launched for this continuation. Phase 1 final evidence existed in `/tmp/krellis-away1final-*`: lifecycle 1/2 outputs, monitors, two-output persistence save/restore, and all twelve baseline scenarios. Repeated lifecycle on one/two outputs at `/tmp/krellis-continue-lifecycle{,-two}` and core 10/10 before proceeding.

Phase 2: hide/restore, dock/float and output transfers capture the currently painted rectangle and opacity. Close captures before releasing its flight and retains fractional opacity; a minimize-visible ref carries a closing hide. Flight dump includes captured `from` and `baseOpacity` for diagnosis. `scenarios/interrupt.sh` asserts early hide reversal, rehide, interrupted dock reversal, closing a glide and closing a hide. Original twelve scenarios and core 10/10 passed at `/tmp/krellis-continue-phase2-*`.

Phase 3 implementation is `effect/native_resize.cpp`. Real KWin interactive resize start/step/finish drives floating normalized geometry or model push-resize boundaries. Preview pins the camera and follows logical pointer deltas, avoiding double scaling and native minimum-size configure jumps. Escape/native cancel restores the saved model; accept preserves floating size in persistence and remembered docking state. Preview is excluded from session writes. Structural changes cancel previews; unloading cancels the native grab and rolls back the model before saving/restoring native state. Floating native rest sizes now respect configured scaling minimums as docked windows do.

`scenarios/resize.sh` starts actual KWin native resize operations through `performMousePressCommand`, then drives `updateInteractiveMoveResize` and native end/cancel. It checks real stepped signals, split commit/cancel, zoomed boundary delta, scaled floating corner delta, opposite corner anchoring, persistence and remembered float geometry. `scenarios/lifecycle.sh` additionally unloads during native resize and independently inspects native geometry and grab state through KWin scripting. No physical mouse injection or pointer warp is needed for the new resize scenario.

Final baseline validation completed under `/tmp/krellis-continue-final-*`: all twelve baseline scenarios, scaled input, rules, panel area, two-output lifecycle, monitors and two-output persistence save/restore exited 0. The new interrupt, resize and one-output expanded lifecycle scenarios also passed. Core 10/10 passes and the effect builds.

Final review additionally preserved opacity through area/rule rebases, made stage wrappers transparent when finding resize dividers, and cancelled previews on rule reload or session restore. The reviewed interrupt and resize scenarios passed at `/tmp/krellis-continue-reviewed-{interrupt,resize}`. A saved test layout with a root split whose first child is a stage wrapping alpha passed the full resize scenario at `/tmp/krellis-continue-reviewed-resize-stage-seeded` using `/tmp/krellis-continue-stage-seed.json`; this directly exercises traversal across a stage wrapper. The first unseeded stage test attempted to grow a divider already at its model minimum limit and correctly clamped; the fixture now uses a negative delta with room to shrink. This failed fixture is not counted as a successful run. Reviewed rules and panel-area scenarios both passed at `/tmp/krellis-continue-reviewed-{rules,area}`. The final interactive preview smoke exited 0; its log is `/tmp/krellis-continue-reviewed-preview-run.txt`. The preview screenshot was inspected. No live installation, commits or pushes.

Interactive preview entry point: `tools/preview.sh`. It opens a separate nested desktop with three terminal panels, its own temporary XDG state and `t` terminal controls. No installation into the live compositor. Close the preview terminals to end it. The `t resize <panel> <dx> <dy>` control accepts a native KWin resize in one command; physical modifier/decoration routing and shortcut grabs still need human testing. Remaining desktop-demo parity gaps (stage-relative floats, app-launching/magnifying dock, reduced motion, collapsed detail, screen-space layout tween trajectories) are unchanged.


## Preview feedback: repaint and decoration input (2026-10-20)

Ivan reported blinking/black rectangles even while idle and normal title-bar drags acting like corner grabs or targeting another window. These exposed gaps in the earlier scenario coverage: screenshots render their own fresh scene and can hide damage tracking bugs; direct `/Krellis` handler simulation did not exercise KWin's complete native input routing.

Corrections:

- `KrellisEffect::prePaintScreen` sets `PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS` and full paint damage. Window-only transform flags were insufficient: surface damage otherwise stayed in native coordinates while windows were painted elsewhere, leaving stale/black regions during partial updates. This uses the generic repaint path for correctness; damage-region optimization remains future work.
- Four-argument Qt hover events now receive window-local scene position first and global position second. Enter/leave is tracked per painted decoration; KWin's pointer-decoration target is synchronized so cursor/section state agrees with the painted window. Press refreshes the section even without a preceding motion event.
- A mapped title-bar grab receives its own release during native move/resize. That grab is exempt from the canvas's own drag-inertness check, so releasing actually ends the native move and clears capture. Native drag start/step uses KWin's interactive anchor instead of an unrelated global cursor query.
- Plugin staging uses copy-to-temp then rename, so a rebuild cannot truncate the plugin mapped by an already running preview. The nested harness accepts `NEST_SOCKET` overrides and refuses occupied sockets; tests can coexist with a user preview without controlling it.

New regression: `scenarios/decorations.sh` plus `pointer-driver.cpp`, `damage-client.cpp`, and the explicit test-only `krellis_paint_probe` target. The pointer fixture refuses anything except an isolated `krellis-test-*` socket with matching temporary config. It drives the actual Wayland fake-input protocol through KWin's complete pipeline, checks native move/resize state independently through KWin scripting, and verifies translated/scaled title centres, sequential captures/releases, visible-client clicks, a floating title and a real corner resize.

The test-only paint probe reads pixels from actual rendered frame buffers without requesting repaint or screenshots. Small cursor-like client damage continues at 100 ms; both ordinary and zoomed idle layouts must preserve solid body colours and show both states of each blinking patch, with no bad samples. This passed at `/tmp/krellis-input-decorations7/out` along with the full real-pointer scenario. Earlier fixture attempts corrected a missing include, fake-input permission setup and framebuffer-transform sampling; those failures are not successful evidence. The input test also directly caught the retained-native-move release bug before the final pass.

Reproduce (permission bypass is scoped to this throwaway nested KWin only):

    PATH=$HOME/.local/bin:$PATH cmake --build build --target krellis krellis_paint_probe -j2
    KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 NEST_SOCKET=krellis-test-decorations \
      NEST_WORK=/tmp/krellis-input-recheck tools/nested.sh build/root \
      krellis,krellis_paint_probe scenarios/decorations.sh 1280 800

Core tests pass 10/10. All twelve baseline scenarios, scaled input, resize and interrupted flights passed under `/tmp/krellis-pointer-fix-*`, as did two-output monitors and the two-output native lifecycle scenario. The final strengthened real-pointer/idle-damage fixture passed under `/tmp/krellis-input-decorations-final`, including a floating-window title and resize corner. Both idle pixel checks observed both blink colours with zero bad samples. No live compositor changes, commits or pushes. Do not claim full hardware or driver coverage from this fixture. No live effect installation or live input injection was performed. Ivan must restart an old preview to load the corrected binary.


## Typing black-out workaround (2026-10-20)

Ivan reported terminal and bar turning black on typing and requested a quick fix. The affected preview `/tmp/krellis-preview.TrI7Oq/out/kwin.log` contained GL_OUT_OF_MEMORY texture allocation and incomplete-framebuffer errors despite over 6 GiB free VRAM. Root cause is not established. Forcing all GL to Mesa software failed EGL initialization on this nested backend and was reverted. `tools/preview.sh` now defaults only `QT_QUICK_BACKEND=software`, retaining normal compositor/client rendering. This bypasses Qt Quick's offscreen OpenGL UI path. A separate preview typed and erased 100 characters through the real fake-input keyboard fixture, exited 0, and captured a valid terminal/bar screenshot with no allocation/framebuffer errors: `/tmp/krellis-software-ui-run.txt`, `/tmp/krellis-preview.fJSDrR/out/preview.png`. This is a scoped preview workaround, not proof that the underlying NVIDIA/context issue is fixed. Preview type smoke uses KRELLIS_PREVIEW_TYPE_TEST=1 and a krellis-test-* socket with test-only protocol permission environment. No broad rerun, live compositor changes or user-preview shutdown.

## Focus-free testing (2026-10-20)

Ivan asked that test windows not steal focus. `tools/nested.sh` now honours `NEST_BACKEND=virtual` (`kwin_wayland --virtual`), which renders to a framebuffer with no outer window. `basic.sh` passed this way with the same layout numbers as the windowed runs and a correct screenshot (`/tmp/krellis-virtual-basic/out`). Other scenarios were not re-run on the virtual backend; fake-input scenarios and the multi-output/scale/rotation ones may behave differently there. The log of Ivan's running preview (`/tmp/krellis-preview.yeF77M/out/kwin.log`) shows no GL or framebuffer errors; whether typing still blacks out is unconfirmed until Ivan says.

## Blinking while moving windows (2026-10-20, in progress)

Ivan saw blinking in a fresh preview while moving windows. That preview's kwin.log again had two `GL_OUT_OF_MEMORY` / `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT` pairs at startup, before any window was added; Ivan's earlier 18:28 preview (started from his own shell) had none, so the GL fault is intermittent. Note `scenarios/preview.sh` only accepts `krellis-nest` or `krellis-test-*` sockets; a second preview needs e.g. `NEST_SOCKET=krellis-test-preview2`, and must be started with Bash `run_in_background`.

New diagnostic `scenarios/drag-damage.sh` (paint probe `watchBlack`/`blackStats`, whole-frame 16x12 grid of near-black samples, virtual backend): idle 0 black frames; while dragging a tiled window 80 of 314 frames had >2% black (max 11%), dragging a float 7 of 100. Mid-drag screenshots show the lifted window's slot left as an unpainted hole (no backdrop under the canvas), which the real framebuffer renders black. This is a plausible but unconfirmed cause of what Ivan sees; he has not yet said where the blinking happens.

## Live-desktop trial support (2026-10-20)

Ivan chose to make a real desktop trial possible. VT test (`tools/vt-preview.sh`, real GPU) showed no blinking, so the blinking seen in the nested preview was a nested-window artefact; the empty-slot hole during drags remains a polish item. Added: install rule (`KRELLIS_PLUGIN_DIR`), `tools/install.sh`, `tools/uninstall.sh`, `tools/live.sh`, the Meta+Alt+Escape Turn Off shortcut, and `scenarios/turnoff.sh` (virtual backend; effect unloads, `/Krellis` disappears, native size restored). Nothing was installed or loaded live. Not yet verified against a real Plasma shell: panels/docks/popups/krunner interplay, and the permission-checked live shortcut grab. First trial should be `tools/install.sh` then `tools/live.sh on`.

## Resize-on-drop fix and optional bar (2026-10-20)

Ivan reported columns resizing on drag/drop. Cause: upstream `removeNode` gives the lifted window's share to the next sibling, then the drop inserts at `1/(n+1)`. Fix is drag-only: proportional removal plus exact restore when the drop leaves the tree shape unchanged (edge-dock drops that land in the same shape also restore, but their live preview still halves the target). Unit test in `test_tree.cpp`; `scenarios/redrop.sh` asserts 415/415/415. Bar now off by default with shortcuts (see AGENTS.md). Verified on the virtual backend only, not yet live. Possible extra cause on Ivan's desktop: vibetiles89 was loaded alongside. Open: preview/commit mismatch for edge drops; closing a window still uses the neighbour-absorbs rule.

## 2026-10-20: edge resize + live drag preview
Done: resize-only-border hit testing (`effect/surface_input.cpp`), real-size drag preview (`applyRest` + `restRect(panel, world)` in `effect/krellis_effect.cpp`), tests `previewsize.sh`, `edge-resize.sh`, `underback.sh` (WHO/UNDER_X/BACK_XS env), unit test `roundTripsUnderAndBackKeepWidths`.
Open: left-window-keeps-shrinking not reproduced; need Ivan's exact steps (and whether vibetiles89 was unloaded).

## Per-install plugin id (2026-10-20)

Ivan's desktop kept running old code after `install.sh` + `live.sh off/on` (live `dump` lacked the new `drawn` field): KWin keeps a mapped plugin file. Fix: `KRELLIS_ID` CMake cache var (metadata.json.in, krellis.qrc.in, `KRELLIS_ID` macro for qrc URLs and `turnOff`); `install.sh` builds `build-live/` with `krellis_<MMDDhhmmss>` and deletes older plugin files; `live.sh on` swaps ids with no logout; new `version` D-Bus method returns the compile stamp. Verified in a nested KWin: load `krellis`, unload, load `krellis_b`, `version` changed and `/Krellis` re-registered. Not yet verified on the real desktop; the first swap from the old `krellis.so` should also work since the new file path was never mapped.

## Handoff 2026-10-20 (late): two-monitor fixes and the float gap

Done and tested: native maximize no longer pushes windows to the other output (`placedByUs`), live cross-output drags (`m_handoff`/`adoptDrag`), `scenarios/maximize2.sh` and `scenarios/cross-drag.sh` (both `NEST_OUTPUTS=2`). Full single-output regression, 11 unit tests, decorations and `persist-monitors` pass. See AGENTS.md "Two monitors, maximize and cross-output drags".

**Open decision (Ivan's):** the demo is float-first (Stage desktop, windows open as stage floats, tiling is opt-in by docking). The effect is tile-first with viewport-fixed overlay floats. Options: (1) build the Stage-as-desktop mode as an opt-in setting; (2) keep tile-first and add cheap float affordances (for example a modifier while dragging to float a window at the drop point, a title-bar float button, a float-by-default rule UI). Option 1 is the faithful port and the larger piece of work (stage-relative `floatScreen`, camera-following, persistence, resize). Details in AGENTS.md "Floats versus the desktop example".

## 2026-10-20 (late): float-first mode, edge fill, panel clamp

Built and tested in the nested harness (all scenarios and 11 unit tests pass), not yet installed live. Details are in AGENTS.md ("Floating-first / tiling-first modes", "Float edge and corner snap"). Summary: `[General] floatFirst` in krellisrc / Meta+Alt+F; float-first opens windows as floats and tiles only on edge (24px) or corner drops, Shift inverts; edge drops with no tiles give a half/quarter that fills the room beside floats; overlay floats are clamped inside the usable area (no more drawing over the Plasma panel); float-over-float drops no longer snap back. Ivan must run `tools/install.sh`, `tools/live.sh on`, and check `live.sh status` stamps match.

## 2026-10-20 (night): blink fix on all glides, CSD edges

Content blink (client laid out at final size ahead of the animation) fixed for camera glides and layout tweens (maximize, docks, drops); see AGENTS.md "No content blink during glides". Brave (CSD) edge resize: `surfaceHit` border pass now covers the surface's shadow band; untested with a real Brave. rules/scaled/lifecycle failures under the virtual backend are environmental (no pointer device), not regressions. Not yet installed live.

2026-10-20 (late): zoom stutter. Stepped client-size requests during glides are now gated per window (`Tracked::requestedMs`, `SteppingPatienceMs`): a client gets a new size once it landed the last one or after 120 ms. Scenarios and ctest pass in the nested virtual backend; needs real-world confirmation with Brave. Ivan also has the "Geometry Change" scripted effect enabled in kwinrc, which animates every programmatic resize and would stack on ours; A/B it off when judging smoothness. Not yet installed live.

## 2026-10-21
Ivan: throttle only slightly better. Added `[General] glide=false` (see AGENTS.md "Glide switch"). Not installed live; Ivan must add the key to krellisrc himself or approve us doing it. If smooth, consider making glide off the default or adding a shorter ease instead of a spring.
2026-10-21 (later): glide key was silently ignored (QSettings [General] prefix); fixed, default is now off, covers flights/appear/popup/close too. Ivan plans three live comparisons: (1) glide off + Geometry Change off, (2) glide off + Geometry Change on, (3) glide on. Not yet confirmed live.

## 2026-10-21: landing animation
Added `landMs` (default 200) short move-and-scale on real frame changes with glide off. Nested tests pass (land-anim, noglide-blink, zoom/maximize-blink, drag, resize, float-fill, anim; glide-on: zoom/maximize-blink, drag, interrupt; ctest 11/11). Needs live confirmation from Ivan. Open: why Geometry Change showed nothing alongside Krellis.

## 2026-10-21: grid zoom and minimap help
Alt/Meta+scroll and plain title-bar scroll now zoom one column and row per gesture (Ctrl+scroll released to apps); minimap shows Floating/Tiling and a hover (?) shortcut sheet; clicking a framed minimap tile zooms out. Details in AGENTS.md "Grid zoom...". Unit tests (18 navigator) and nested scenarios zoom-grid, map, input, zoom-blink, noglide-blink, bar-optional pass. Not committed, not installed live: Ivan runs `tools/install.sh` + `tools/live.sh off/on` (logout if status shows MISMATCH).

## 2026-10-21: plan 05 implemented (polish and safety)

All seven items of `docs/plans/05-polish-and-safety.md` are implemented, built and covered by nested scenarios; nothing is committed, installed or loaded live. See its "Implementation notes" for deviations and `AGENTS.md` ("Plan 05") for the mechanics. Open for Ivan: install and try it (`tools/install.sh`, `tools/live.sh on`), confirm Meta+Alt+Shift+Z as redo, and check by hand the three things the harness cannot (global chord dismissing the which-key sheet, no sheet on a fast Meta+Alt+Key, modifier delivery while another client holds the keyboard). Enable permanently only with the safe-mode guard in the build you enable.


## 2026-10-21: tiny windows
Ivan: tiles stop shrinking, so the fractal does not go deep. Measured in the nested harness (26 Alacritty on 2560 px): cutoff was the icon swap under 160x64 px; Brave (client min 540) was drawn letterboxed. Added `iconBelow` (0 = always live) and uniform scale-up of held clients in `restRect`; see AGENTS.md "Tiny windows". Not installed live. Ivan: run `tools/install.sh`, `tools/live.sh on`, and set `iconBelow=0` in `[General]` of `~/.config/krellisrc` (or the KCM) to try it; the default stays 160. Open: whether live should become the default; camera zoom-out is still capped at 1.35x the root.

## 2026-10-21: tile minimum
Follow-up to "tiny windows": Ivan's Dolphin spiral stopped deepening because of the 320x200 default layout minimum, not Dolphin. Now `tileMinWidth`/`tileMinHeight` (default 1, KCM "Smallest tile"). Behaviour change for existing layouts: with many windows the weights are followed exactly instead of being evened out at the old floor. Open: whether tiny tiles (<160 px) should stay icons by default (`iconBelow`), and the 1.35x zoom-out cap.

## 2026-10-22: live is the default
Ivan confirmed the tile minimum works live and asked for tiny windows to stay live: `iconBelow` now defaults to 0 (icon swap is opt-in, e.g. 160). Zoom-out cap 1.35x stays; float-first default unchanged. Finding 4 (split axis by tile shape for pointer placement) still undecided; Ivan did not understand it, so it was explained to him. `icons.sh` now seeds `scenarios/icons.rc`.

## 2026-10-22: focus on close
Ivan: closing a window should focus its parent. Meaning chosen with him: the nearest sibling in the tree (previous, else next). Added `closeFocus` setting (default on), `closeFocusPanel`, unit test `closeFocusGoesToNearestSibling`, scenario `close-focus.sh`. Not installed live.
