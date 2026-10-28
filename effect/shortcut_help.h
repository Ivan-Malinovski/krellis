// The keyboard-only cheat sheet behind the which-key hint: which registered shortcuts to list, how to word them and
// where to group them. The chords themselves come from KGlobalAccel at show time, so a rebound key shows as bound.
#pragma once

namespace krellis {

struct HelpRow {
    const char *id;       // the QAction object name in registerShortcuts()
    const char *label;
    bool arrows = false;  // one of four directional actions: the chord is shown with "Arrows" as its last key
};

struct HelpGroup {
    const char *name;
    const HelpRow *rows;
    int count;
};

inline constexpr HelpRow kHelpNavigate[] = {
    {"KrellisPalette", "Jump to window or view"},
    {"KrellisUrgent", "Jump to window needing attention"},
    {"KrellisOverview", "Overview"},
    {"KrellisBack", "Back"},
    {"KrellisForward", "Forward"},
    {"KrellisStepOut", "Step out"},
    {"KrellisNextPanel", "Next panel"},
    {"KrellisPreviousPanel", "Previous panel"},
    {"KrellisFocusLeft", "Focus panel", true},
};
inline constexpr HelpRow kHelpArrange[] = {
    {"KrellisMoveLeft", "Move panel", true},
    {"KrellisResizeMode", "Resize mode"},
    {"KrellisMaximize", "Maximize"},
    {"KrellisFloat", "Float"},
    {"KrellisPin", "Pin to the corner"},
    {"KrellisNextTab", "Next tab"},
    {"KrellisPreviousTab", "Previous tab"},
    {"KrellisGroupLeft", "Group into tabs", true},
    {"KrellisSplitTab", "Split tab out"},
    {"KrellisFlip", "Flip split"},
    {"KrellisStash", "Stash in edge drawer"},
    {"KrellisHide", "Hide to dock"},
    {"KrellisClose", "Close"},
    {"KrellisUndo", "Undo layout edit"},
    {"KrellisRedo", "Redo layout edit"},
};
inline constexpr HelpRow kHelpInterface[] = {
    {"KrellisBar", "Toggle bar"},
    {"KrellisMap", "Toggle map"},
    {"KrellisMode", "Floating / tiling first"},
    {"KrellisSaveView", "Save view"},
    {"KrellisOff", "Turn off Krellis"},
};
inline constexpr HelpRow kHelpScratch[] = {
    {"KrellisScratchpad", "Scratchpad"},
    {"KrellisScratchMark", "Mark as scratchpad"},
};

inline constexpr HelpGroup kHelpGroups[] = {
    {"Navigate", kHelpNavigate, int(sizeof(kHelpNavigate) / sizeof(HelpRow))},
    {"Arrange", kHelpArrange, int(sizeof(kHelpArrange) / sizeof(HelpRow))},
    {"Interface", kHelpInterface, int(sizeof(kHelpInterface) / sizeof(HelpRow))},
};

} // namespace krellis
