# Window zoom blink: handoff after rollback

Updated 2026-10-30. This is an investigation handoff, **not a description of a successful fix**. Ivan rejected every resize-policy change made during this conversation and requested a complete rollback. Preserve the restored smoothness while investigating the remaining blink.

## Current state and scope

- Source baseline: `e308e79` — `Improve deep-zoom performance and dwindle behavior`.
- All code, installer, test and experimental-document changes from the blink investigation were reverted to that baseline. The worktree was clean before this documentation task.
- Installed and loaded effect: `krellis_1030150556`.
- Running and installed stamps match: `build Oct 30 2026 15:05:58`, verified again while preparing this handoff.
- Ivan's feedback after rollback: **“nice and smooth but with a blink.”** This is the accepted starting point, not a claim that the issue is fixed.
- The separate Aurorae cache patch and earlier performance fixes predate this investigation and were not reverted. Last recorded decoration is `org.kde.kwin.aurorae.krellis_1030123148` with FieldworkDark; see [the Aurorae investigation](aurorae-cache-fix-2026-10-30.md).
- No commits or pushes were made during the blink experiments. Build IDs below are installation identifiers, not Git revisions.
- This task authorizes documentation only. Do not infer authorization to install another experimental fix from approval of the rejected builds or the rollback. Follow the current conversation and `AGENTS.md` for any subsequent activation.

The goal is to remove a brief content blink during zoom enlargement **without changing the currently smooth navigation into slow, stepped, stretched, cropped or disconnected window motion**. Manual edge dragging is an important reference: Ivan repeatedly reports that it is smooth and pleasant.

## What the user sees

During zoom enlargement, a window very briefly appears to show another part/layout of its own contents. The baseline otherwise feels smooth. Browser windows are a relevant real workload; later recordings contain several readable browser windows moving and changing width on a 3440×1440 desktop.

This is a visual observation. It has not been established whether the original blink is entirely a layout/paint-transform mismatch, a surface/buffer-coordinate issue, or another client/compositor interaction. Do not relabel it as GPU tearing, damage corruption or a specific Chromium bug without evidence.

## Recordings: distinguish the baseline from regressions

These files remain local under `/home/ivan/Videos/Screencasts/`. Do not publish their contents; they contain the user's desktop. Inspect frame by frame or at reduced playback speed.

| Recording | What it represents |
| --- | --- |
| `Screencast_20261030_134159.webm` | Original smooth baseline with the enlargement blink. Useful starting reproduction. |
| `Screencast_20261030_135124.webm` | Delayed final configure: pronounced stretching through enlargement, followed by a layout snap. Rejected. |
| `Screencast_20261030_141102.webm` | Intermediate native resizing with dimensions tied to committed buffers. Ivan said the original issue was gone but many windows now stuttered. |
| `Screencast_20261030_144206.webm` | Percentage-limited size pacing. Position settles while size continues changing, leaving gaps and long tails. Ivan called this the worst so far. |

`Screencast_20261030_134108.webm` also exists but was not the main evidence used. **The newest recording is not a recording of the restored baseline.** There is no newer recording in this folder of the final native-path attempt or of the rollback as of this handoff; those later judgments came from Ivan's feedback.

For local inspection, for example:

```bash
ffprobe -v error -show_entries format=duration \
  /home/ivan/Videos/Screencasts/Screencast_20261030_134159.webm
ffmpeg -i /home/ivan/Videos/Screencasts/Screencast_20261030_134159.webm \
  -vf 'setpts=4*PTS' -an /tmp/krellis-blink-slow.mp4
```

## Relevant baseline implementation

Read these functions in the restored checkout; line numbers refer to `e308e79`:

| Location | Role |
| --- | --- |
| `effect/krellis_effect.cpp:820`, `restRect` | Native requested rectangle, including client size constraints and buffer-size limits. |
| `effect/krellis_effect.cpp:906`, `finishCameraResize` | Deferred gesture/spring completion. It waits for a gesture or camera spring, not for every screen-space presentation flight. |
| `effect/krellis_effect.cpp:923`, `applyRest` | A discrete Hyprland camera destination requests visible clients' destination geometry immediately. Held gestures/springs retain buffers until release/rest. Offscreen, pinned, icon-only and background-tab clients are skipped in navigation-only sizing. |
| `effect/krellis_effect.cpp:1029`, `syncWindowMotion` | Presentation retargeting and captured source rectangles, independent of client acknowledgements. |
| `effect/krellis_effect.cpp:1745`, `flightRect` | Fixed-duration presentation interpolation. Hyprland movement uses a 479ms front-loaded Bézier. |
| `effect/krellis_effect.cpp:1850`, `prePaintScreen` | Frame advancement, geometry refresh decisions and flight cleanup. |
| `effect/krellis_effect.cpp:1978`, `paintWindow` | Paint transform uses drawn/native width and height independently. |
| `effect/krellis_effect.cpp:2125`, `visualRect` | Paint/input presentation geometry. Ordinary Hyprland flights fit per axis; cross-output transitions have an existing temporary contain fit. |
| `effect/native_resize.cpp` | Actual native border resize: `startResize`, `resizeAt`, `finishResize`, and the test-only `simNativeResize*` slots. |
| `effect/surface_input.cpp` | Input and popup transforms; any painted-rectangle change must remain consistent with these. |

**Working hypothesis, not a completed diagnosis:** an early destination configure can produce a newly reflowed buffer while the painted rectangle is still at an intermediate size. The ratio used by the paint transform changes when native frame geometry changes. This fits the observed content shrink/grow, but changing configure timing repeatedly moved the defect or introduced worse motion. No definitive frame-by-frame correlation of configure serial, committed geometry, actual surface/buffer geometry and presented browser content was completed.

## Rejected approaches

| Installed build | Attempt | Live outcome |
| --- | --- | --- |
| `krellis_1030135013`, 13:50:15 | Hold the old native layout until the camera presentation finishes, then request the final size. | Old narrow contents stretch across enlargement, followed by a late reflow/snap. The initial interior-only pixel test missed this. |
| `krellis_1030140953`, 14:09:55 | Resize through the flight with acknowledgement/120ms patience pacing; paint dimensions follow committed buffer dimensions, with captured uniform scale for previously scaled buffers. | Original blink reported gone, but many windows became stuttery. Position still followed the ideal flight while size followed client commits. |
| `krellis_1030144114`, 14:41:16 | Limit native steps to 2.5%, interpolate each step using preceding buffer latency; prepare tiny/offscreen sources before enlargement. | Separate position and size clocks caused seconds-long size tails, gaps and poor interrupted motion. Reduced configure counts in one workload did not mean better feel. |
| `krellis_1030150205`, 15:02:07 | Remove percentage pacing; send full rectangles along a 479ms smoothstep; paint complete committed frames including x/y. Preserve offscreen preparation and scaled-source handling. | Passed delayed Qt and actual Brave tests, but Ivan still reported it very sluggish and not working. Rejected and fully reverted. |
| `krellis_1030150556`, 15:05:58 | Rebuild/install original source. | Ivan confirms smoothness is back, with the original blink. Current baseline. |

Do not reinstate one of these implementations merely because it passed a fixture. An earlier uniform cover/cropping experiment, predating this conversation, was also rejected: vertical-only zoom magnified width and cut off contents. Aspect-fit/letterbox experiments had produced objectionable gaps. Preserve these constraints when choosing a new direction.

## Why the verification was insufficient

1. **A marker remaining visible did not rule out stretching.** The first test only sampled inside a fixed green square. A 2× stretched square still covers that point. Later tests added points just outside and inside the intended boundaries.
2. **Bounded distortion did not rule out unusably slow motion.** The 2.5% experiment passed expansion/compression checks but could take several seconds with delayed clients. A later 900ms completion check caught the preceding implementation, but its replacement still felt sluggish live.
3. **Native commit counts did not establish smoothness.** The last rejected implementation produced 27 native width changes during zoom versus 25 during a real border drag in a private Brave test. Its phases completed in 527–562ms; delayed Qt phases took 582–742ms. These were real measurements, but they did not establish the quality of the user's interaction. The manual and zoom displacement were not identical, and counts hide spacing, stalls and interrupted-input behavior.
4. **Low compositor CPU time and fewer configures were not sufficient.** One 25-window workload went from roughly 524 configure requests to 24 after offscreen-source preparation. The build was still rejected. Work counts must be considered alongside the complete visual trajectory and user feedback.
5. **Most tests used virtual 60Hz outputs.** They did not establish physical-display high-refresh feel, real input cadence or the user's live webpages. The actual-Brave fixture used three local pages with 1500 responsive cards each in one disposable profile; it was still not the live workload.
6. **Screenshots render another scene.** They are useful for inspection, but do not prove idle damage correctness or frame-by-frame presentation. Use the existing paint probe for real presented-frame evidence.

The experiments passed all 20 CTests and multiple targeted nested scenarios. These establish useful model/input/rendering properties; they do not overrule Ivan's rejection. No claim of a working blink fix remains.

## Manual resizing: useful next comparison, with an important distinction

Ivan asked whether zoom could use the behavior that already works when dragging edges. The final rejected attempt used `Window::moveResize` and complete native frames, but **calling the same geometry method is not equivalent to reproducing the whole interactive resize path**.

The local KWin 6.7.5 reference source shows:

- `third_party/kwin-src/src/window.cpp:1146`, `Window::updateInteractiveMoveResize`: interactive geometry computation, synchronization checks and stepped signals.
- `third_party/kwin-src/src/window.cpp:2405`, `Window::doInteractiveResizeSync`: base implementation calls `moveResize`.
- `third_party/kwin-src/src/xdgshellwindow.cpp:938`, `XdgToplevelWindow::doStartInteractiveMoveResize`: sets resize gravity and the xdg-toplevel `Resizing` state and schedules configure. Finish clears the state.
- `third_party/kwin-src/src/xdgshellwindow.cpp:140`, `:200`, `:262`: configure acknowledgement/commit handling, geometry application, and `moveResizeInternal`.

These differences were read, **not isolated as the cause**. A stronger comparison would match the same client, start/end rectangles, duration and input sequence, recording individual configure/commit/presentation timings and relevant state. Do not manufacture global interactive pointer grabs just to toggle a client flag: KWin's move/resize ownership and input lifecycle must stay correct.

## Reproduction/test material

Existing baseline fixtures remain: `camera-coverage`, `camera-resize`, `zoom-depth`, `zoom-load`, `zoom-burst`, `hyprland`, `cross-aspect`, `resize`, `interrupt`, `output-damage`, `animation-cost-map`, and `icons-reentry`. Their current assertions describe the restored behavior. Read the scenario and `.rc`/`.env` before interpreting a pass.

The experimental `camera-flash` and `camera-native` scenarios, the additional probe fields and the three experiment-summary JSON files were **removed by the rollback**. They are not present in the checkout and cannot be invoked without reconstruction. Do not follow commands in an old tool log that assume they still exist.

Useful reconstruction details, if needed:

- The flash fixture used two Qt windows, a 100×100 green square at client (0,0), and a `#643d3d` background. It sampled `(70,70)`, `(105,70)`, `(70,105)`, `(95,70)`, `(70,95)`, transformed through the actual client/frame offsets and drawn rectangle. Test horizontal and vertical enlargement plus interrupted focus/overview reversals. Fixed screen points require an anchored source; account for decoration offsets.
- A `--paint-delay 40` option deliberately delayed Qt paint events. Shared-process windows can serialize these delays; this is a stress workload, not a browser timing model.
- The wide fixture used three readable windows at 3440×1440, exercised `simNativeResizeStart/Move/End` as a baseline, then focus, overview and interrupted retargets. Opt-in probe data recorded native x/y/w/h and painted x/y/w/h on each actual frame.
- The Brave fixture used `/usr/bin/brave-origin`, Wayland, local HTML only, and a unique profile inside its nested work directory. It never joined/copied Ivan's profile. The disposable profile needed its free-on-Linux Origin startup screen dismissed (`brave.origin.free_tier_accepted`) and a welcome preference set before launch. The startup failure before doing this was a fixture issue, not a Krellis regression.

### Ephemeral evidence still on this machine

These paths were checked while writing this handoff. They may disappear after reboot or cleanup; the facts above are intended to remain understandable without them. Do not load old experimental binaries into the live session.

| Path | Contents |
| --- | --- |
| `/tmp/krellis-flash-followup/` | Earlier delayed-configure and committed-buffer investigations, logs and comparisons. |
| `/tmp/krellis-zoom-stutter/` | Percentage-pacing experiments, controls, installation logs and workload comparisons. |
| `/tmp/krellis-resize-rework/` | Final rejected native-path logs and preceding-implementation control. `before.cpp` / `before.h` preserve the earlier percentage-pacing source, not the restored baseline. |
| `/tmp/krellis-resize-rework/control/out/log.txt` | Preceding implementation passes manual baseline setup, then fails to settle native zoom geometry. |
| `/tmp/krellis-scn.256yiQ/camera-flash/out/` | Final native-path delayed-paint pixels/timing data: 24 assertions passed. |
| `/tmp/krellis-scn.t032dr/camera-native/out/` | Delayed Qt native-resize comparison, `manual.json`, phase samples and `result.json`. |
| `/tmp/krellis-scn.67PXFo/camera-native/out/` | Actual private-profile Brave comparison: 25 manual native changes, 27 zoom changes; phase timing and settled screenshot. This build was nevertheless rejected live. |
| `/tmp/krellis-revert-install.log` | Installation log for the restored baseline now running. |

## Suggested next investigation

1. Reproduce and characterize the **baseline blink** using the original recording and an isolated reproducer before changing resize policy. Keep a source/build/recording mapping for every comparison.
2. Correlate configure request, acknowledgement, native frame geometry, client/buffer geometry, actual painted transform and content pixels through the offending frames. The transition between client layouts is a lead, not a complete explanation.
3. Compare a matched manual resize and zoom with the same real client and geometry trajectory. Look at individual update spacing and state, not just total calls or final geometry.
4. Prefer a narrowly demonstrated rendering/synchronization correction over another broad timing/resize-policy rewrite. Retain the smooth baseline as the control; test interrupted gestures, multiple readable windows and both outputs.
5. Judge blink, distortion, gaps, completion latency and perceived motion together. Keep test failures that expose a regression instead of weakening them to fit the implementation. Fixture success remains provisional until actual live feedback.

No particular new fix is recommended as proven by this handoff.

## Workflow and related documentation

Follow `~/AGENTS.md` and the repository `AGENTS.md`. Experimental effects belong in a private nested compositor with `NEST_BACKEND=virtual`, a distinct task-owned socket/work directory and at most three concurrent scenarios. Use task-owned clients and profiles for reproduction; do not copy personal browser data or change personal settings.

Core checks and an example focused run using fixtures that still exist:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target krellis_paint_probe
tools/scenarios.sh -j 2 -k camera-coverage camera-resize hyprland resize interrupt output-damage
```

Use the repo's documented local CMake prefixes for KWin builds. Do not enable a new live build without applicable authorization. When authorized, perform install, `tools/live.sh on`, and `tools/live.sh status` together; never leave the new build installed but unloaded. No commits or pushes unless requested.

The one-password developer-installer change requested during the conversation was also removed by the complete rollback. `tools/install.sh` is back to its original separate privileged install/cleanup calls. Treat any future installer improvement as an independent task; do not reintroduce rejected rendering changes with it.

Related background, whose older live-state notes are historical:

- [Earlier performance handoff](performance-handoff-2026-10-30.md)
- [Zoom performance investigation](zoom-performance.md)
- [Hyprland animation behavior at the restored baseline](hyprland-animations.md)
- [Separate Aurorae cache correction](aurorae-cache-fix-2026-10-30.md)
- [Earlier Kitty transparency investigation](kitty-transparency-investigation-2026-10-30.md)
