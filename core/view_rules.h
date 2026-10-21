#pragma once
#include "spatial.h"
namespace trellis {
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
inline bool iconTile(const Rect &rect) { return rect.w < 160 || rect.h < 64; }
}
