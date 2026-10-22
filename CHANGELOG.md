# Changelog

## 0.0.2

- A tiled window that goes fullscreen (a Brave video, a game) now covers the whole output instead of being scaled into its tile, gets native input, and returns to its tile afterwards. The overlays (bar, minimap, toasts) are hidden while a window is fullscreen.

## 0.0.1

First tagged release.

- Tiled layout on an infinite canvas with a spring-driven camera, one canvas per monitor.
- Drag docking, floating windows, floating-first mode, edge drawers, an opt-in scratchpad, minimap, saved views, undo and redo.
- Keyboard movement (Meta+arrows or Meta+A/S/D/F) and focus (Meta+Ctrl+arrows), with hand-over to the next monitor and a 20% shrink at a dead end.
- Session restore, per-application rules, Plasma theme following, and a settings module.
- `install.sh` builds and installs the effect and the settings module; `--uninstall` removes them.
