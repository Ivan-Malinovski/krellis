You are continuing a project in `/home/ivan/dev/krellis`: a clean-room port of DanFessler/trellis's "desktop" demo (infinite canvas, camera over a tiled layout, springs, docking, minimap) to KDE Plasma 6 / KWin Wayland, written as an out-of-tree KWin effect (C++20, Qt6, QML).

Start by reading, in order: `HANDOFF.md`, `AGENTS.md`, then `~/AGENTS.md` (machine rules). Follow them exactly. In particular:
- Do not copy Trellis code (noncommercial license); follow its semantics only. References are read-only at `~/dev/trellis_kde/trellis/packages/core/src/` and `~/dev/trellis_kde/trellis/examples/desktop/src/`.
- No commits unless asked, never push, never read or print secrets, check `df -h /` before big builds, never delete caches or containers.
- Test only in the nested KWin harness (`tools/nested.sh`), never load the effect into the live session.
- Shell is fish; use `export PATH=$HOME/.local/bin:$PATH` before cmake builds of the effect.

Every visible change needs a sleek animation matching the Krellis desktop demo (spring 210/29, layout tween 460 ms easeOutQuint, appear 220 ms, dock hide 300 ms, restore 380 ms, close 180 ms).

Work through the "Not done / open" list in `HANDOFF.md`, in this order unless you find a reason to change it:
1. Real Plasma work area (respect Plasma panels instead of the fixed 48 px bar-only reduction), then multi-monitor.
2. Inert scaling and scaled pointer input for windows during camera gestures, and popups of scaled windows.
3. Saved-framing name entry (text input for the bar's Save button), view-type rules, small-panel icon tiles, `toggleDock`.
4. Persistence of layout across sessions (design a stable window identity first; ask Ivan if the approach is unclear).
5. Port more Krellis tests to `tests/`.

For each item: implement, add or extend a scenario under `scenarios/`, run it plus the existing scenarios (basic, anim, many, hide, flights, input, drag, float, rect, bar, map, keys) and `ctest --test-dir build-core`, and fix regressions before moving on. Update `AGENTS.md` and `HANDOFF.md` as you go. Keep status text short and keep working without waiting for confirmation, but stop and report if something needs real hardware or a decision only Ivan can make.
