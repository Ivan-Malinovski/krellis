#include "trellis_effect.h"
#include "effect/effecthandler.h"
#include "effect/offscreenquickview.h"
#include "window.h"
#include <QSettings>
#include <QStandardPaths>
#include <QQuickItem>
#include <QBuffer>
#include <QGuiApplication>
#include <QStyleHints>
#include <QRegion>
#include <cmath>

namespace KWin {
using namespace trellis;

QString TrellisCanvas::appId(EffectWindow *w)
{
    if (!w || !w->window()) return {};
    const auto desktop = w->window()->desktopFileName();
    return desktop.isEmpty() ? w->window()->resourceClass() : desktop;
}
void TrellisCanvas::loadRules()
{
    m_rules.clear();
    m_rulesFile = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/trellisrc");
    QSettings config(m_rulesFile, QSettings::IniFormat);
    config.sync();
    // Materialize the INI sections before querying nested groups. Qt parses
    // sections lazily; invalid syntax must never produce a partial policy.
    const auto keys = config.allKeys();
    if (config.status() != QSettings::NoError) return;
    config.beginGroup(QStringLiteral("ViewRules"));
    for (const QString &app : config.childGroups()) {
        config.beginGroup(app);
        ViewRules rule;
        const auto place = config.value("placement", "side").toString();
        rule.placementSet = config.contains("placement");
        if (place == "stage") rule.placement = ViewPlacement::Stage;
        if (place == "floating") rule.placement = ViewPlacement::Floating;
        const auto scaling = config.value("scaling", "interactive").toString();
        if (scaling == "inert") rule.scaling = ViewScaling::Inert;
        if (scaling == "false" || scaling == "none") rule.scaling = ViewScaling::None;
        auto dimension = [&config](const char *name, double fallback) {
            bool ok = false;
            const double v = config.value(name, fallback).toDouble(&ok);
            return ok && std::isfinite(v) && v >= 1 && v <= 16384 ? v : fallback;
        };
        rule.minimum = {dimension("minWidth", 160), dimension("minHeight", 120)};
        rule.side = config.value("allowSide", true).toBool();
        rule.stage = config.value("allowStage", true).toBool();
        rule.floating = config.value("allowFloating", true).toBool();
        rule.singleton = config.value("singleton", false).toBool();
        rule.closable = config.value("closable", true).toBool();
        rule.scratchpad = config.value("scratchpad", false).toBool();
        m_rules[app] = rule;
        config.endGroup();
    }
}
bool TrellisCanvas::scratchWindow(EffectWindow *w) const
{
    if (!w || !m_scratchpad) return false;
    if (m_markedScratch && m_markedScratch(w)) return true;
    const auto found = m_rules.find(appId(w));
    return found != m_rules.end() && found->second.scratchpad;
}
ViewRules TrellisCanvas::rulesFor(EffectWindow *w) const
{
    const auto found = m_rules.find(appId(w));
    ViewRules rule = found == m_rules.end() ? ViewRules{} : found->second;
    if (scratchWindow(w)) {
        // Floating only: it never joins the tiled layout, and a drag can't dock it.
        rule.scratchpad = true;
        rule.side = rule.stage = false;
        rule.floating = true;
        rule.placement = ViewPlacement::Floating;
        rule.placementSet = true;
    }
    return rule;
}
bool TrellisCanvas::layerShown() const
{
    return std::any_of(m_windows.begin(), m_windows.end(), [this](const Tracked &t) {
        return !t.window->isMinimized() && !t.window->isHidden() && scratchWindow(t.window);
    });
}
bool TrellisCanvas::floatForScratch(EffectWindow *w, int slot)
{
    if (m_resize) finishResize(false);
    const Tracked *t = trackedFor(w);
    if (!t) return false;
    const auto loc = locatePanel(m_doc, t->panel);
    if (loc.where == PanelLocation::Where::Floating || loc.where == PanelLocation::Where::Hidden) return true;
    if (loc.where != PanelLocation::Where::Docked || leafIds(m_doc.root).size() < 2) return false;
    double opacity = 1;
    const TRect from = visualRect(w, &opacity).value_or(screenRect(t->panel));
    const auto saved = m_rememberedFloats.find(t->panel);
    const TRect rect = saved == m_rememberedFloats.end() ? TRect{0.2 + 0.03 * (slot % 5), 0.175 + 0.04 * (slot % 5), 0.6, 0.65} : saved->second;
    m_doc = floatPanel(m_doc, loc.panel, rect, FloatingLayer::Overlay);
    relayout(true);
    applyRest();
    startFlight(w, FlightKind::Glide, from, {}, opacity);
    return true;
}
Point TrellisCanvas::minSize(const std::vector<std::string> &views) const
{
    Point minimum{0, 0};
    for (const auto &view : views)
        for (const auto &t : m_windows) if (t.view == view) {
            const auto rule = rulesFor(t.window);
            if (rule.scaling == ViewScaling::None) continue;
            const auto size = m_rules.contains(appId(t.window)) ? rule.minimum : Point{320, 200};
            minimum.x = std::max(minimum.x, size.x);
            minimum.y = std::max(minimum.y, size.y);
        }
    return minimum;
}
bool TrellisCanvas::allowed(const std::vector<std::string> &views, trellis::Region region) const
{
    const auto where = region == trellis::Region::Stage ? ViewPlacement::Stage
        : region == trellis::Region::Floating ? ViewPlacement::Floating : ViewPlacement::Side;
    for (const auto &view : views)
        for (const auto &t : m_windows)
            if (t.view == view && !rulesFor(t.window).allows(where)) return false;
    return true;
}
bool TrellisCanvas::frameOnly(const std::string &panel) const
{
    return !(m_session && m_session->active() && m_session->lifted()->id == panel) && iconTile(screenRect(panel));
}

void TrellisCanvas::toggleDockActive() { toggleDock(activeIndex()); }
void TrellisCanvas::toggleDock(int index)
{
    if (m_resize) finishResize(false);
    if (index < 0 || index >= int(m_windows.size())) return;
    const auto t = m_windows[size_t(index)];
    const auto location = locatePanel(m_doc, t.panel);
    const auto rule = rulesFor(t.window);
    if (!location || location.where == PanelLocation::Where::Hidden) return;
    double opacity = 1;
    const TRect from = visualRect(t.window, &opacity).value_or(screenRect(t.panel));
    if (location.where == PanelLocation::Where::Floating) {
        if (!rule.side) return;
        m_rememberedFloats[t.panel] = location.floatState.rect;
        const auto stage = findStage(m_doc.root);
        m_doc = removePanel(m_doc, t.panel);
        if (!vacancies(m_doc.root).empty()) {
            dockAtEnd(location.panel);
        } else if (stage && findNode(m_doc.root, stage->id)) {
            TRect shown{0, 0, m_area.width(), m_area.height()};
            if (const auto *rect = worldRect(stage->id)) shown = panelAt(*rect, m_camera.value);
            m_doc = insertPanel(m_doc, location.panel, DockTarget::beside(stage->id, shown.w >= shown.h ? trellis::Edge::Right : trellis::Edge::Bottom));
        } else dockAtEnd(location.panel);
        relayout(true);
        m_nav.include(stage ? std::vector<std::string>{stage->id, t.panel} : std::vector<std::string>{t.panel});
    } else {
        if (!rule.floating) return;
        const auto saved = m_rememberedFloats.find(t.panel);
        const TRect rect = saved == m_rememberedFloats.end() ? TRect{0.2, 0.15, 0.6, 0.65} : saved->second;
        // Float-first leaves the slot empty for the next window; tiling reflows the neighbours into it.
        if (const auto vacated = m_floatFirst ? vacatePanel(m_doc, t.panel) : std::nullopt)
            m_doc = *vacated;
        m_doc = floatPanel(m_doc, location.panel, rect, FloatingLayer::Overlay);
        relayout(true);
        if (const auto stage = findStage(m_doc.root)) m_nav.include({stage->id});
    }
    applyRest();
    startFlight(t.window, FlightKind::Glide, from, {}, opacity);
    effects->activateWindow(t.window);
}

void TrellisCanvas::updateTiles(double dt)
{
    QStringList ids;
    for (const auto &t : m_windows) ids << QString::fromStdString(t.panel);
    if (ids.isEmpty() && !m_tileScene) return;
    if (!m_tileScene) {
        m_tileScene = std::make_unique<OffscreenQuickScene>();
        m_tileScene->setSource(QUrl(QStringLiteral("qrc:/" TRELLIS_ID "/tiles.qml")));
        if (!m_tileScene->rootItem()) { m_tileScene.reset(); return; }
    }
    m_tileScene->setGeometry(Rect(m_area.toAlignedRect()));
    auto *root = m_tileScene->rootItem();
    if (ids != m_tileIds) {
        m_tileIds = ids;
        root->setProperty("panels", ids);
    }
    std::erase_if(m_tiles, [this](const auto &tile) {
        return std::none_of(m_windows.begin(), m_windows.end(), [&](const auto &t) { return t.panel == tile.first; });
    });
    for (size_t i = 0; i < m_windows.size(); ++i) {
        const auto &t = m_windows[i];
        auto &tile = m_tiles[t.panel];
        const double target = frameOnly(t.panel) && !t.window->isMinimized() ? 1 : 0;
        tile.target = target;
        const double h = std::clamp(dt, 0.0, Motion::maxStep);
        tile.velocity += (Motion::stiffness * (target - tile.amount) - Motion::damping * tile.velocity) * h;
        tile.amount += tile.velocity * h;
        if (std::abs(target - tile.amount) < 0.0001 && std::abs(tile.velocity) < 0.001) { tile.amount = target; tile.velocity = 0; }
        if (tile.icon.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (!t.window->icon().isNull()) t.window->icon().pixmap(48, 48).save(&buffer, "PNG");
            tile.icon = bytes.isEmpty() ? QStringLiteral("") : QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
        }
        double opacity = 1;
        const auto rect = visualRect(t.window, &opacity).value_or(TRect{});
        QRegion visible(QRectF(0, 0, rect.w, rect.h).toAlignedRect());
        bool above = false;
        for (auto *window : effects->stackingOrder()) {
            if (window == t.window) { above = true; continue; }
            if (!above || window->isDeleted() || window->isMinimized() || window->isHidden()) continue;
            if (const auto cover = visualRect(window))
                visible -= QRectF(cover->x - rect.x, cover->y - rect.y, cover->w, cover->h).toAlignedRect();
        }
        QVariantList pieces;
        for (const QRect &piece : visible)
            pieces << QVariantMap{{"x", piece.x()}, {"y", piece.y()}, {"w", piece.width()}, {"h", piece.height()}};
        QVariantMap properties{{"pieces", pieces}, {"x", rect.x - m_area.x()}, {"y", rect.y - m_area.y()}, {"w", rect.w}, {"h", rect.h},
            {"amount", std::clamp(tile.amount, 0.0, 1.0) * opacity}, {"icon", tile.icon}, {"title", t.window->caption()}};
        QMetaObject::invokeMethod(root, "setTile", Q_ARG(QVariant, int(i)), Q_ARG(QVariant, properties));
    }
}

bool TrellisCanvas::peeking(const std::string &panel) const
{
    if (m_peekPx <= 0 || m_session || m_nav.overviewing() || floatScreen(panel) || !m_rects.find(panel)) return false;
    const auto &inside = m_nav.framedLeaves();
    return std::find(inside.begin(), inside.end(), panel) == inside.end();
}

// A click on the edge of a tile that is out of focus widens the framing to take it in.
bool TrellisCanvas::handlePeekButton(const PointerButtonEvent &event)
{
    if (event.button != Qt::LeftButton) return false;
    if (event.state == PointerButtonState::Released && !m_peekPressed.empty()) {
        const auto panel = m_peekPressed;
        m_peekPressed.clear();
        for (const auto &t : m_windows) if (t.panel == panel) {
            if (const auto rect = visualRect(t.window); rect && QRectF(rect->x, rect->y, rect->w, rect->h).contains(event.position)) {
                m_nav.include(trellis::leafIds(m_rects.find(panel)->node));
                applyRest();
            }
            break;
        }
        return true;
    }
    if (event.state != PointerButtonState::Pressed || m_peekPx <= 0 || overUi(event.position)) return false;
    const auto stack = effects->stackingOrder();
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        auto *window = *it;
        if (window->isDeleted() || window->isMinimized() || window->isHidden()) continue;
        const auto rect = visualRect(window);
        if (rect && QRectF(rect->x, rect->y, rect->w, rect->h).contains(event.position)) {
            const auto *tracked = trackedFor(window);
            if (!tracked || !peeking(tracked->panel)) return false;
            m_peekPressed = tracked->panel;
            return true;
        }
        if (!rect && window->frameGeometry().contains(event.position) && !window->isDesktop()) return false;
    }
    return false;
}

bool TrellisCanvas::handleIconButton(const PointerButtonEvent &event)
{
    if (event.button != Qt::LeftButton) return false;
    if (event.state == PointerButtonState::Released && !m_iconPressed.empty()) {
        const auto panel = m_iconPressed;
        m_iconPressed.clear();
        if (m_session) endDrag(true);
        else {
            for (const auto &t : m_windows) if (t.panel == panel) { effects->activateWindow(t.window); break; }
            if (m_lastIconClick == panel && m_nowMs - m_lastIconClickMs < QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
                m_nav.focus(panel);
                applyRest();
                m_lastIconClick.clear();
            } else { m_lastIconClick = panel; m_lastIconClickMs = m_nowMs; }
        }
        return true;
    }
    if (event.state != PointerButtonState::Pressed || (event.modifiers & (Qt::ControlModifier | Qt::AltModifier)) == (Qt::ControlModifier | Qt::AltModifier)) return false;
    if (overUi(event.position)) return false;
    const auto stack = effects->stackingOrder();
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        auto *window = *it;
        if (window->isDeleted() || window->isMinimized() || window->isHidden()) continue;
        const auto rect = visualRect(window);
        if (rect && QRectF(rect->x, rect->y, rect->w, rect->h).contains(event.position)) {
            const auto *tracked = trackedFor(window);
            if (!tracked || !frameOnly(tracked->panel)) return false;
            m_iconPressed = tracked->panel;
            m_iconAnchor = event.position;
            return true;
        }
        if (!rect && window->frameGeometry().contains(event.position) && !window->isDesktop()) return false;
    }
    return false;
}
bool TrellisCanvas::handleIconMotion(const PointerMotionEvent &event)
{
    if (m_iconPressed.empty()) return false;
    if (!m_session && (event.position - m_iconAnchor).manhattanLength() > trellis::DragSession::Threshold) {
        for (const auto &t : m_windows) if (t.panel == m_iconPressed) { startDrag(t.window, m_iconAnchor); break; }
    }
    if (m_session) moveDrag(event.position);
    return true;
}

void TrellisEffect::reloadRules()
{
    for (const auto &canvas : m_canvases) {
        if (canvas->m_resize) canvas->finishResize(false);
        std::map<EffectWindow *, std::pair<TRect, double>> before;
        for (const auto &t : canvas->m_windows)
            if (!t.window->isMinimized()) {
                double opacity = 1;
                const auto rect = canvas->visualRect(t.window, &opacity).value_or(TRect{});
                before[t.window] = {rect, opacity};
            }
        canvas->loadRules();
        canvas->relayout(true);
        for (const auto &[window, paint] : before)
            canvas->startFlight(window, TrellisCanvas::FlightKind::Glide, paint.first, {}, paint.second);
    }
}

} // namespace KWin
