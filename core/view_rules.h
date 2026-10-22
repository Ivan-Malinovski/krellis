#pragma once
#include "spatial.h"
namespace krellis {
enum class ViewPlacement { Side, Stage, Floating };
enum class ViewScaling { Interactive, Inert, None };
struct ViewRules {
    ViewPlacement placement = ViewPlacement::Side;
    bool placementSet = false; // a rule names a placement; otherwise the mode decides where new windows open
    ViewScaling scaling = ViewScaling::Interactive;
    Point minimum{160, 120};
    bool side = true, stage = true, floating = true;
    bool singleton = false, closable = true;
    bool scratchpad = false; // a floating overlay summoned and dismissed with the scratchpad shortcut
    bool allows(ViewPlacement where) const {
        switch (where) {
        case ViewPlacement::Side: return side;
        case ViewPlacement::Stage: return stage;
        case ViewPlacement::Floating: return floating;
        }
        return false;
    }
    bool interactive(double scale) const { return scaling != ViewScaling::Inert || scale >= 0.999; }
};
/// A tile too small to read shows the app icon instead of its contents. `below` is the width in pixels (the height
/// limit is 40% of it); 0 or less keeps every window live however small it is drawn.
inline bool iconTile(const Rect &rect, double below = 160) { return below > 0 && (rect.w < below || rect.h < below * 0.4); }
}
