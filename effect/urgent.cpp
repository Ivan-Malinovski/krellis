// Windows that ask for attention. KWin raises "demands attention" itself (a denied focus request, a chat client, a
// finished build) and clears it when the window is activated, so Krellis only reads the flag: an amber outline on
// the tile (motion_overlay), an amber tile on the minimap, first place in the palette, and Meta+U to jump to one.
#include "krellis_effect.h"

#include "effect/effecthandler.h"
#include "window.h"

namespace KWin {
using namespace krellis;

bool KrellisCanvas::isUrgent(EffectWindow *w) const
{
    if (!m_urgentOn || !w || w->isDeleted() || !w->window()) return false;
    if (w == effects->activeWindow()) return false;
    return w->window()->isDemandingAttention();
}

bool KrellisCanvas::urgentPanel(const std::string &panel) const
{
    if (const auto *t = presentedPanel(panel); t && isUrgent(t->window)) return true;
    for (const auto &t : m_stowed) // a tab behind the shown one still counts
        if (t.panel == panel && isUrgent(t.window)) return true;
    return false;
}

int KrellisCanvas::urgentCount() const
{
    int n = 0;
    for (const auto &t : m_windows) n += isUrgent(t.window);
    for (const auto &t : m_stowed) n += isUrgent(t.window);
    return n;
}

void KrellisCanvas::urgentChanged(EffectWindow *w)
{
    if (m_unloading || !m_urgentOn) return;
    if (!trackedFor(w) && !stowedFor(w)) return;
    m_mapSignature.clear();
    updateMap();
    if (isUrgent(w)) {
        QString app = appId(w);
        app = app.mid(app.lastIndexOf(QLatin1Char('.')) + 1);
        toast(QStringLiteral("%1 needs attention").arg(w->caption().isEmpty() ? app : w->caption()));
    }
    schedule();
}

void KrellisCanvas::setUrgentOn(bool on)
{
    if (m_urgentOn == on) return;
    m_urgentOn = on;
    m_mapSignature.clear();
    updateMap();
    schedule();
}

} // namespace KWin
