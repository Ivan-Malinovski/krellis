// Tabbed tiles (opt-in, `tabs`). A panel can hold several windows. The document says which is selected; the canvas keeps
// that one in m_windows, where everything else (layout, painting, input, the bar) already looks for windows, and parks
// the rest in m_stowed: sized like the shown one so a switch is instant, but never painted and never hit. The strip along
// the top of the tile is an overlay scene; the pointer goes through tabAt(), so the strips take no input of their own.
#include "krellis_effect.h"
#include "effect/effecthandler.h"
#include "effect/offscreenquickview.h"
#include <KGlobalAccel>
#include <QAction>
#include <QLoggingCategory>
#include <QQuickItem>
#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(krellisLog)

namespace KWin {
using namespace krellis;

KrellisCanvas::Tracked *KrellisCanvas::stowedFor(EffectWindow *w)
{
    return const_cast<Tracked *>(static_cast<const KrellisCanvas *>(this)->stowedFor(w));
}

const KrellisCanvas::Tracked *KrellisCanvas::stowedFor(const EffectWindow *w) const
{
    if (const auto it = m_stowedIndex.find(w); it != m_stowedIndex.end())
        if (it->second < m_stowed.size() && m_stowed[it->second].window == w)
            return &m_stowed[it->second];
    for (const auto &t : m_stowed)
        if (t.window == w) return &t;
    return nullptr;
}

KrellisCanvas::Tracked *KrellisCanvas::trackedOfView(const std::string &view)
{
    for (auto *list : {&m_windows, &m_stowed})
        for (auto &t : *list)
            if (t.view == view) return &t;
    return nullptr;
}

bool KrellisCanvas::behindTab(EffectWindow *w) const
{
    if (!w || m_stowed.empty()) return false;
    if (stowedFor(w)) return !(m_session && m_session->active() && w == m_dragWindow); // the one in hand is drawn
    if (w->isPopupWindow())
        if (auto *parent = w->transientFor(); parent && parent != w) return behindTab(parent);
    return false;
}

double KrellisCanvas::tabInset(const std::string &panel) const
{
    if (!m_tabsOn || !m_tabbed.count(panel)) return 0;
    const TRect r = screenRect(panel);
    return r.h > 0 ? std::min(TAB_H, r.h * 0.25) : 0;
}

// Where a dropped window becomes a tab: the strip when the tile has one, else the band a strip would fill (the title bar).
double KrellisCanvas::tabDropHeight(const krellis::Node &panel) const
{
    if (!m_tabsOn) return 0;
    const auto it = lastRects().find(panel.id);
    return it == lastRects().end() || it->second.h <= 0 ? 0 : std::min(TAB_H, it->second.h * 0.25);
}

TRect KrellisCanvas::belowTabs(const std::string &panel, TRect rect) const
{
    if (!m_tabsOn || !m_tabbed.count(panel)) return rect;
    const double h = std::min(TAB_H, rect.h * 0.25);
    rect.y += h;
    rect.h -= h;
    return rect;
}

// The tab of a panel that is on show: the selected view, or the first when none is.
static std::string shownView(const NodePtr &panel)
{
    if (panel->views.empty()) return {};
    return std::find(panel->views.begin(), panel->views.end(), panel->selected) != panel->views.end() ? panel->selected : panel->views.front();
}

// Brings every window into line with the document: the panel it is in, and whether it is the tab on show. Runs first in
// relayout, so everything after it sees one shown window per panel.
void KrellisCanvas::syncTabs()
{
    const auto panels = allPanels(m_doc); // by value: keep one copy, a second call would give iterators into another
    const bool anyTabbed = std::any_of(panels.begin(), panels.end(), [](const NodePtr &p) { return p->views.size() > 1; });
    if (!m_tabsOn && !m_session && anyTabbed)
        m_doc = untabAll(m_doc); // tabs were switched off (or a saved session had some): one tile per window
    m_tabbed.clear();
    if (m_tabsOn)
        for (const auto &p : panels)
            if (p->views.size() > 1) m_tabbed.insert(p->id);
    if (m_tabbed.empty() && m_stowed.empty()) return;

    const auto panelOf = [this](const Tracked &t) { return panelOfView(m_doc, t.view); };
    for (auto *list : {&m_windows, &m_stowed})
        for (auto &t : *list)
            if (const NodePtr p = panelOf(t)) t.panel = p->id;
    std::set<std::string> known; // every view that has a window, before the lists are taken apart
    for (auto *list : {&m_windows, &m_stowed})
        for (const auto &t : *list) known.insert(t.view);
    const auto exists = [&known](const std::string &view) { return known.count(view) > 0; };

    std::vector<Tracked> stow = std::move(m_stowed), next;
    next.reserve(m_windows.size());
    m_stowed.clear();
    const auto takeStowed = [&stow](const std::string &view) -> std::optional<Tracked> {
        const auto it = std::find_if(stow.begin(), stow.end(), [&](const Tracked &s) { return s.view == view; });
        if (it == stow.end()) return std::nullopt;
        Tracked t = std::move(*it);
        stow.erase(it);
        return t;
    };
    // A window that is no longer the tab on show steps back, and the tab that is takes its place in the list.
    for (auto &t : m_windows) {
        const NodePtr p = panelOf(t);
        const std::string want = p ? shownView(p) : t.view;
        if (!p || want == t.view || !exists(want)) { next.push_back(std::move(t)); continue; }
        EffectWindow *w = t.window;
        m_flights.erase(w);
        m_landings.erase(w);
        effects->setElevatedWindow(w, false);
        auto replacement = takeStowed(want);
        stow.push_back(std::move(t));
        if (replacement) { replacement->lastDrawn.reset(); next.push_back(std::move(*replacement)); }
    }
    std::set<std::string> present;
    for (const auto &t : next) present.insert(t.panel);
    for (auto it = stow.begin(); it != stow.end();) {
        const NodePtr p = panelOf(*it);
        if (p && shownView(p) == it->view && !present.count(p->id)) {
            present.insert(p->id);
            it->lastDrawn.reset();
            next.push_back(std::move(*it));
            it = stow.erase(it);
        } else {
            ++it;
        }
    }
    m_windows = std::move(next);
    m_stowed = std::move(stow);

    // A pin follows the tab on show.
    for (Pin &pin : m_pins) {
        const Tracked *shown = nullptr;
        for (const auto &t : m_windows)
            if (t.panel == pin.panel) { shown = &t; break; }
        if (!shown || shown->window == pin.window) continue;
        if (pin.shown) {
            if (pin.window) effects->setElevatedWindow(pin.window, false);
            effects->setElevatedWindow(shown->window, true);
        }
        pin.window = shown->window;
    }
}

void KrellisCanvas::setTabsOn(bool on)
{
    if (m_tabsOn == on) return;
    if (m_session) endDrag(false);
    m_tabsOn = on;
    m_tabPress.reset();
    if (!on) {
        EditScope edit(*this, "split tabs");
        m_doc = untabAll(m_doc);
    }
    relayout(true);
    updateDock();
}

void KrellisCanvas::selectTab(const std::string &view, bool activate)
{
    if (m_session || m_resize) return;
    const NodePtr panel = panelOfView(m_doc, view);
    if (!panel || panel->views.size() < 2) return;
    if (shownView(panel) != view) {
        m_doc = selectView(m_doc, view);
        relayout(false);
    }
    if (!activate) return;
    for (const auto &t : m_windows)
        if (t.view == view) { jumpTo(t.window); return; }
}

void KrellisCanvas::cycleTab(int step)
{
    const int i = activeIndex();
    if (i < 0 || m_session) return;
    const NodePtr panel = panelOfView(m_doc, m_windows[size_t(i)].view);
    if (!panel || panel->views.size() < 2) return;
    const long n = long(panel->views.size());
    const auto at = std::find(panel->views.begin(), panel->views.end(), shownView(panel)) - panel->views.begin();
    selectTab(panel->views[size_t(((at + step) % n + n) % n)]);
}

void KrellisCanvas::closeTab(const std::string &view)
{
    Tracked *t = trackedOfView(view);
    if (t && rulesFor(t->window).closable) t->window->closeWindow();
}

// The active window, with the tab it holds, joins the tile in that direction as another tab of it.
bool KrellisCanvas::groupWithNeighbour(Edge dir)
{
    const int i = activeIndex();
    if (!m_tabsOn || i < 0 || m_session || m_resize) return false;
    const Tracked &active = m_windows[size_t(i)];
    if (floatScreen(active.panel)) { toast(QStringLiteral("A floating window cannot join a tile's tabs this way")); return true; }
    const std::string to = neighbourInDirection(m_rects, active.panel, dir);
    if (to.empty()) return false; // nothing that way: the chord is free for the caller to carry on
    const std::string view = active.view;
    {
        EditScope edit(*this, "group into tabs");
        m_doc = tabInto(m_doc, view, to);
    }
    relayout(true);
    updateDock();
    if (const Tracked *t = trackedOfView(view)) jumpTo(t->window);
    return true;
}

void KrellisCanvas::splitTabOut()
{
    const int i = activeIndex();
    if (!m_tabsOn || i < 0 || m_session || m_resize) return;
    const std::string view = m_windows[size_t(i)].view;
    const NodePtr panel = panelOfView(m_doc, view);
    if (!panel || panel->views.size() < 2) {
        toast(QStringLiteral("This window has no tabs to leave"));
        return;
    }
    {
        EditScope edit(*this, "split tab out");
        m_doc = untabView(m_doc, view);
    }
    relayout(true);
    updateDock();
    if (const Tracked *t = trackedOfView(view)) jumpTo(t->window);
}

// ------------------------------------------------------------------ strips

int KrellisCanvas::tabIndexAt(const Node &panel, double x) const
{
    const auto it = lastRects().find(panel.id);
    const double n = double(std::max<size_t>(1, panel.views.size()));
    if (it == lastRects().end() || it->second.w <= 0) return int(panel.views.size());
    const double tw = std::min(TAB_MAX_W, it->second.w / n);
    return std::clamp(int(std::lround((x - it->second.x) / tw)), 0, int(panel.views.size()));
}

void KrellisCanvas::computeStrips()
{
    m_strips.clear();
    if (!m_tabsOn || m_tabbed.empty() || m_overlaysCovered || m_unloading) return;
    const bool dragging = m_session && m_session->active();
    for (const auto &id : m_tabbed) {
        const auto where = locatePanel(m_doc, id);
        if (where.where == PanelLocation::Where::None || where.where == PanelLocation::Where::Hidden || !where.panel) continue;
        if (m_drawers.count(id)) continue;
        const Tracked *shown = nullptr;
        for (const auto &t : m_windows)
            if (t.panel == id) { shown = &t; break; }
        if (!shown || shown->window->isFullScreen() || shown->window->isMinimized() || shown->window->isHidden()) continue;
        if (dragging && m_dragWindow == shown->window && m_dragTabView.empty()) continue; // the whole tile is in hand
        const bool floating = floatScreen(id).has_value();
        if (!floating && !worldRect(id)) continue;
        const TRect r = screenRect(id);
        if (r.w < 48 || r.h < 2 * TAB_H) continue;
        if (!QRectF(r.x, r.y, r.w, r.h).intersects(m_area)) continue;
        Strip strip;
        strip.panel = id;
        strip.rect = {r.x, r.y, r.w, std::min(TAB_H, r.h * 0.25)};
        strip.selected = shownView(where.panel);
        for (const auto &view : where.panel->views) {
            if (view == m_dragTabView) continue;
            const Tracked *t = nullptr;
            for (auto *list : {&m_windows, &m_stowed})
                for (const auto &c : *list)
                    if (c.view == view) t = &c;
            strip.views.push_back(view);
            const QString caption = t ? t->window->caption() : QString();
            strip.titles.push_back(caption.isEmpty() ? std::string("Untitled") : caption.toStdString());
        }
        if (strip.views.empty()) continue;
        strip.tabW = std::min(TAB_MAX_W, strip.rect.w / double(strip.views.size()));
        // An overlay float over a strip hides it: the strip is an overlay itself and would draw across the float.
        bool covered = false;
        for (const auto &f : m_doc.floating) {
            if (f.layer != FloatingLayer::Overlay || f.panel->id == id) continue;
            if (floating) {
                const auto mine = std::find_if(m_doc.floating.begin(), m_doc.floating.end(), [&](const FloatingPanel &p) { return p.panel->id == id; });
                if (mine != m_doc.floating.end() && f.z <= mine->z) continue; // only what is above it
            }
            const auto fr = floatScreen(f.panel->id);
            if (fr && QRectF(fr->x, fr->y, fr->w, fr->h).intersects(QRectF(strip.rect.x, strip.rect.y, strip.rect.w, strip.rect.h))) { covered = true; break; }
        }
        if (covered) continue;
        m_strips.push_back(std::move(strip));
    }
}

void KrellisCanvas::updateTabStrips()
{
    computeStrips();
    if (m_strips.empty()) {
        if (m_tabStrips) {
            repaintRegion(Region(m_tabStrips->geometry()));
            m_tabStrips.reset();
        }
        m_tabStripsSignature.clear();
        return;
    }
    if (!m_tabStrips) {
        m_tabStrips = makeScene(QStringLiteral("tabstrips.qml"));
        if (!m_tabStrips || !m_tabStrips->rootItem()) {
            qCWarning(krellisLog) << "tabstrips.qml failed to load";
            m_tabStrips.reset();
            return;
        }
        m_tabStrips->setVisible(!m_overlaysCovered);
        m_tabStripsSignature.clear();
    }
    m_tabStrips->setGeometry(Rect(m_area.toAlignedRect()));
    QVariantList list;
    QString signature;
    for (const auto &s : m_strips) {
        QVariantList tabs;
        for (size_t i = 0; i < s.views.size(); ++i)
            tabs << QVariantMap{{QStringLiteral("title"), QString::fromStdString(s.titles[i])}, {QStringLiteral("selected"), s.views[i] == s.selected}};
        list << QVariantMap{{QStringLiteral("x"), s.rect.x - m_area.x()}, {QStringLiteral("y"), s.rect.y - m_area.y()}, {QStringLiteral("w"), s.rect.w},
                            {QStringLiteral("h"), s.rect.h}, {QStringLiteral("width"), s.tabW}, {QStringLiteral("tabs"), tabs}};
        signature += QStringLiteral("%1,%2,%3,%4,%5,%6;").arg(s.rect.x).arg(s.rect.y).arg(s.rect.w).arg(s.rect.h).arg(s.tabW).arg(QString::fromStdString(s.selected));
        for (const auto &title : s.titles) signature += QString::fromStdString(title) + QLatin1Char('|');
    }
    if (signature == m_tabStripsSignature) return;
    m_tabStripsSignature = signature;
    m_tabStrips->rootItem()->setProperty("strips", list);
    repaintRegion(Region(m_tabStrips->geometry()));
}

std::optional<KrellisCanvas::TabHit> KrellisCanvas::tabAt(QPointF p) const
{
    for (auto it = m_strips.rbegin(); it != m_strips.rend(); ++it) {
        if (!QRectF(it->rect.x, it->rect.y, it->rect.w, it->rect.h).contains(p)) continue;
        const size_t index = size_t(std::max(0.0, (p.x() - it->rect.x) / it->tabW));
        return TabHit{it->panel, index < it->views.size() ? it->views[index] : std::string()};
    }
    return std::nullopt;
}

bool KrellisCanvas::tabButton(const PointerButtonEvent &e)
{
    if (!m_tabsOn) return false;
    if (e.button == Qt::LeftButton && e.state == PointerButtonState::Released && m_tabPress) {
        const TabPress press = *m_tabPress;
        m_tabPress.reset();
        if (m_session) endDrag(true);
        else if (!press.view.empty() && !press.bar) selectTab(press.view);
        return true;
    }
    if (e.state != PointerButtonState::Pressed || m_session || m_resize) return false;
    if (e.button != Qt::LeftButton && e.button != Qt::MiddleButton) return false;
    if ((e.modifiers & (Qt::ControlModifier | Qt::AltModifier)) == (Qt::ControlModifier | Qt::AltModifier)) return false; // the pan gesture
    if (overUi(e.position)) return false;
    const auto hit = tabAt(e.position);
    if (!hit) return false;
    if (e.button == Qt::MiddleButton) {
        if (!hit->view.empty()) closeTab(hit->view);
        m_swallowMiddle = true; // the release is ours too
        return true;
    }
    // The bare part of a strip drags the tab on show, like a title bar.
    if (hit->view.empty()) {
        const NodePtr panel = findNode(m_doc.root, hit->panel);
        m_tabPress = TabPress{hit->panel, panel ? shownView(panel) : std::string(), e.position, true};
    } else {
        m_tabPress = TabPress{hit->panel, hit->view, e.position};
    }
    return true;
}

bool KrellisCanvas::tabMotion(QPointF p)
{
    if (!m_tabPress) return false;
    if (!m_session && !m_tabPress->view.empty() && (p - m_tabPress->at).manhattanLength() > DragSession::Threshold) {
        const std::string view = m_tabPress->view;
        if (Tracked *t = trackedOfView(view)) {
            EffectWindow *w = t->window;
            // The tab in hand must not be the one on show: the tile goes on showing its neighbour.
            if (const NodePtr panel = panelOfView(m_doc, view); panel && panel->views.size() > 1 && shownView(panel) == view) {
                const auto at = std::find(panel->views.begin(), panel->views.end(), view) - panel->views.begin();
                m_doc = selectView(m_doc, panel->views[size_t(at + 1 < long(panel->views.size()) ? at + 1 : at - 1)]);
                relayout(false);
            }
            m_dragTabView = view;
            startDrag(w, m_tabPress->at, view);
            if (!m_session) m_dragTabView.clear();
        }
    }
    if (m_session) moveDrag(p);
    return true;
}

// ------------------------------------------------------------------ coordinator

// Registered on demand: tabs are opt-in, so a disabled one holds none of these chords.
void KrellisEffect::syncTabShortcuts()
{
    if (!m_tabsOn) {
        qDeleteAll(m_tabActions);
        m_tabActions.clear();
        return;
    }
    if (!m_tabActions.isEmpty()) return;
    struct Binding { const char *id, *text; QKeySequence keys; void (KrellisEffect::*slot)(); };
    const Binding bindings[] = {
        {"KrellisNextTab", "Krellis: Next Tab", QKeySequence(Qt::META | Qt::Key_PageDown), &KrellisEffect::nextTab},
        {"KrellisPreviousTab", "Krellis: Previous Tab", QKeySequence(Qt::META | Qt::Key_PageUp), &KrellisEffect::previousTab},
        {"KrellisSplitTab", "Krellis: Split Tab Out", QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_U), &KrellisEffect::splitTab},
        {"KrellisGroupLeft", "Krellis: Group Into Tabs Left", QKeySequence(Qt::META | Qt::CTRL | Qt::SHIFT | Qt::Key_Left), &KrellisEffect::groupLeft},
        {"KrellisGroupRight", "Krellis: Group Into Tabs Right", QKeySequence(Qt::META | Qt::CTRL | Qt::SHIFT | Qt::Key_Right), &KrellisEffect::groupRight},
        {"KrellisGroupUp", "Krellis: Group Into Tabs Up", QKeySequence(Qt::META | Qt::CTRL | Qt::SHIFT | Qt::Key_Up), &KrellisEffect::groupUp},
        {"KrellisGroupDown", "Krellis: Group Into Tabs Down", QKeySequence(Qt::META | Qt::CTRL | Qt::SHIFT | Qt::Key_Down), &KrellisEffect::groupDown},
    };
    for (const Binding &b : bindings) {
        auto *a = new QAction(this);
        a->setObjectName(QString::fromLatin1(b.id));
        a->setText(QString::fromLatin1(b.text));
        KGlobalAccel::self()->setGlobalShortcut(a, b.keys);
        connect(a, &QAction::triggered, this, [this, slot = b.slot] { hideHint(); (this->*slot)(); });
        m_tabActions << a;
    }
}

void KrellisEffect::nextTab() { if (!m_tabsOn) return; markSessionInteraction(); if (auto *canvas = current()) canvas->cycleTab(1); }
void KrellisEffect::previousTab() { if (!m_tabsOn) return; markSessionInteraction(); if (auto *canvas = current()) canvas->cycleTab(-1); }
void KrellisEffect::splitTab() { if (!m_tabsOn) return; markSessionInteraction(); if (auto *canvas = current()) canvas->splitTabOut(); }

} // namespace KWin
