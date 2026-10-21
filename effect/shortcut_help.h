// The keyboard-only cheat sheet behind the which-key hint: which registered shortcuts to list, how to word them and
// where to group them. The chords themselves come from KGlobalAccel at show time, so a rebound key shows as bound.
#pragma once

namespace trellis {

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
    {"TrellisOverview", "Overview"},
    {"TrellisBack", "Back"},
    {"TrellisForward", "Forward"},
    {"TrellisStepOut", "Step out"},
    {"TrellisNextPanel", "Next panel"},
    {"TrellisPreviousPanel", "Previous panel"},
    {"TrellisFocusLeft", "Focus panel", true},
};
inline constexpr HelpRow kHelpArrange[] = {
    {"TrellisMoveLeft", "Move panel", true},
    {"TrellisResizeMode", "Resize mode"},
    {"TrellisMaximize", "Maximize"},
    {"TrellisFloat", "Float"},
    {"TrellisFlip", "Flip split"},
    {"TrellisStash", "Stash in edge drawer"},
    {"TrellisHide", "Hide to dock"},
    {"TrellisClose", "Close"},
    {"TrellisUndo", "Undo layout edit"},
    {"TrellisRedo", "Redo layout edit"},
};
inline constexpr HelpRow kHelpInterface[] = {
    {"TrellisBar", "Toggle bar"},
    {"TrellisMap", "Toggle map"},
    {"TrellisMode", "Floating / tiling first"},
    {"TrellisSaveView", "Save view"},
    {"TrellisOff", "Turn off Trellis"},
};
inline constexpr HelpRow kHelpScratch[] = {
    {"TrellisScratchpad", "Scratchpad"},
    {"TrellisScratchMark", "Mark as scratchpad"},
};

inline constexpr HelpGroup kHelpGroups[] = {
    {"Navigate", kHelpNavigate, int(sizeof(kHelpNavigate) / sizeof(HelpRow))},
    {"Arrange", kHelpArrange, int(sizeof(kHelpArrange) / sizeof(HelpRow))},
    {"Interface", kHelpInterface, int(sizeof(kHelpInterface) / sizeof(HelpRow))},
};

} // namespace trellis
