#include "krellis_effect.h"
#include "effect/effecthandler.h"
#include "core/output.h"
#include "window.h"
#include "krellis_config.h"
#include <QStandardPaths>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <set>

namespace KWin {
using namespace krellis;

QString KrellisEffect::outputKey(LogicalOutput *output) const
{
    if (output->serialNumber().isEmpty()) return QStringLiteral("connector:") + output->name();
    const QString serial = output->manufacturer() + QLatin1Char('|') + output->model() + QLatin1Char('|') + output->serialNumber();
    int copies = 0;
    for (auto *other : effects->screens())
        copies += other->manufacturer() + QLatin1Char('|') + other->model() + QLatin1Char('|') + other->serialNumber() == serial;
    return QStringLiteral("monitor:") + serial + (copies > 1 ? QLatin1Char('|') + output->name() : QString());
}
QString KrellisEffect::sessionStatus() const
{
    return QStringLiteral("SESSION complete %1 matched %2 ambiguous %3 pending %4 error '%5'")
        .arg(m_restoreComplete).arg(m_sessionMatches).arg(m_sessionAmbiguous).arg(m_pendingUnique).arg(m_sessionError);
}
void KrellisEffect::initializeSession()
{
    m_sessionFile = krellis::config::stateFile(QStringLiteral("session.json"));
    m_saveTimer = new QTimer(this); m_saveTimer->setSingleShot(true);
    connect(m_saveTimer, &QTimer::timeout, this, [this] {
        if (m_restoreComplete && (m_userTouched || m_pendingUnique == 0)) writeSession();
    });
    m_restoreTimer = new QTimer(this); m_restoreTimer->setSingleShot(true);
    connect(m_restoreTimer, &QTimer::timeout, this, &KrellisEffect::restoreSession);
    QFile file(m_sessionFile);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) { m_sessionError = QStringLiteral("Cannot read saved session"); return; }
    const auto saved = decodeSession(file.read(2 * 1024 * 1024 + 1), &m_sessionError);
    if (saved) { m_savedSession = *saved; m_restoreComplete = m_savedSession.empty(); }
}
void KrellisEffect::sessionWindowOpened()
{
    if (!m_savedSession.empty() && !m_userTouched && m_sessionError.isEmpty()) {
        m_restoreComplete = false;
        m_saveTimer->stop();
        m_restoreTimer->start(750); // Wait for a startup batch, including duplicates.
    } else scheduleSave();
}
void KrellisEffect::scheduleSave()
{
    if (!m_shuttingDown && !m_restoring && m_restoreComplete && m_sessionError.isEmpty()) m_saveTimer->start(750);
}
void KrellisEffect::markSessionInteraction()
{
    if (m_restoring) return;
    m_userTouched = true;
    m_restoreComplete = true;
    m_restoreTimer->stop();
    scheduleSave();
}
bool KrellisEffect::saveSession()
{
    markSessionInteraction();
    return writeSession();
}
bool KrellisEffect::writeSession()
{
    if (!m_sessionError.isEmpty()) return false; // Preserve unreadable/corrupt state for inspection.
    for (const auto &canvas : m_canvases) if (canvas->m_resize) return false; // Never save a resize preview.
    SessionSnapshot snapshot;
    size_t windows = 0;
    for (const auto &canvas : m_canvases) {
        if (!canvas->m_output) continue;
        SavedCanvas saved;
        saved.output = outputKey(canvas->m_output).toStdString();
        // Tabs are not saved: each window comes back in a tile of its own (the format has one view per panel).
        saved.document = untabAll(canvas->m_doc);
        saved.document.navigation = canvas->m_nav.serialize();
        for (const auto *list : {&canvas->m_windows, &canvas->m_stowed})
        for (const auto &t : *list) {
            if (!t.window->window()) continue;
            const NodePtr panel = panelOfView(saved.document, t.view);
            if (!panel) continue;
            saved.windows[panel->id] = {KrellisCanvas::appId(t.window).toStdString(), t.window->window()->windowRole().toStdString()};
            ++windows;
        }
        snapshot.push_back(std::move(saved));
    }
    // Logout often closes clients before effects are destroyed. Keep the last
    // nonempty snapshot rather than replacing it with that teardown sequence.
    if (!windows) return false;
    if (!QDir().mkpath(QFileInfo(m_sessionFile).absolutePath())) return false;
    QSaveFile file(m_sessionFile);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = encodeSession(snapshot);
    return file.write(bytes) == bytes.size() && file.commit();
}
void KrellisEffect::restoreSession()
{
    for (const auto &canvas : m_canvases) if (canvas->m_resize) canvas->finishResize(false);
    m_restoreTimer->stop();
    if (m_savedSession.empty() || !m_sessionError.isEmpty()) { m_restoreComplete = true; return; }
    QScopedValueRollback<bool> restoring(m_restoring, true);
    struct Slot { size_t canvas; std::string panel; };
    std::vector<Slot> savedSlots;
    std::vector<WindowIdentity> previous, live;
    std::vector<EffectWindow *> windows;
    std::map<EffectWindow *, TRect> before;
    std::set<EffectWindow *> wasHidden;
    std::map<WindowIdentity, size_t> multiplicity;
    for (size_t i=0; i<m_savedSession.size(); ++i)
        for (const auto &[panel, key] : m_savedSession[i].windows) {
            savedSlots.push_back({i,panel}); previous.push_back(key); ++multiplicity[key];
        }
    // Count unmanaged singleton duplicates too; a hidden native window is
    // still an ambiguous identity and must not silently pick a saved slot.
    for (auto *window : effects->stackingOrder()) {
        if (!KrellisCanvas::manageable(window) || !window->window()) continue;
        windows.push_back(window);
        live.push_back({KrellisCanvas::appId(window).toStdString(), window->window()->windowRole().toStdString()});
        if (auto *canvas = owner(window)) before[window] = canvas->visualRect(window).value_or(TRect{});
        if (window->isMinimized()) wasHidden.insert(window);
    }
    const auto matches = uniqueWindowMatches(previous, live);
    m_sessionMatches = 0; m_sessionAmbiguous = 0; m_pendingUnique = 0;
    for (const auto &[key,count] : multiplicity) {
        if (key.app.empty() || count > 1) m_sessionAmbiguous += int(count);
        else ++m_pendingUnique;
    }
    std::map<KrellisCanvas *, std::map<std::string, NodePtr>> bindings;
    std::map<KrellisCanvas *, size_t> savedFor;
    std::set<EffectWindow *> matched;
    for (const auto &[savedIndex, liveIndex] : matches) {
        const auto &slot = savedSlots[savedIndex]; const auto &saved = m_savedSession[slot.canvas];
        KrellisCanvas *target = nullptr;
        for (const auto &c : m_canvases)
            if (c->m_output && outputKey(c->m_output).toStdString() == saved.output) { target=c.get(); break; }
        if (!target) continue;
        auto *window=windows[liveIndex];
        const auto location=locatePanel(saved.document,slot.panel);
        auto placement=location.where==PanelLocation::Where::Floating ? ViewPlacement::Floating : ViewPlacement::Side;
        const auto stage=findStage(saved.document.root);
        if (stage && stage->child && findNode(stage->child,slot.panel)) placement=ViewPlacement::Stage;
        if (location.where==PanelLocation::Where::Hidden) {
            if (location.restore.kind==RestoreTarget::Kind::Floating) placement=ViewPlacement::Floating;
            else if (stage && location.restore.panel==stage->id) placement=ViewPlacement::Stage;
        }
        if (!target->rulesFor(window).allows(placement)) continue;
        if (owner(window)!=target) transfer(window,target);
        const auto *tracked=target->trackedFor(window);
        if (!tracked) continue;
        bindings[target][slot.panel]=makePanel(tracked->panel,{tracked->view},tracked->view);
        savedFor[target]=slot.canvas;
        matched.insert(window);
        ++m_sessionMatches; --m_pendingUnique;
    }
    for (auto &[canvas, replacement] : bindings) {
        auto remaining=canvas->m_doc;
        for (const auto &t : canvas->m_windows) if (matched.contains(t.window)) remaining=removePanel(remaining,t.panel);
        auto restored=rebindSession(m_savedSession[savedFor.at(canvas)].document,replacement);
        const auto navigation=restored.navigation;
        canvas->m_doc=std::move(restored);
        canvas->clearHistory(); // the saved layout is the new starting point
        for (const auto &panel : panelsOf(remaining.root)) canvas->dockAtEnd(panel);
        canvas->m_doc.floating.insert(canvas->m_doc.floating.end(),remaining.floating.begin(),remaining.floating.end());
        canvas->m_doc.hidden.insert(canvas->m_doc.hidden.end(),remaining.hidden.begin(),remaining.hidden.end());
        canvas->m_flights.clear();
        canvas->relayout(true);
        for (const auto &t : canvas->m_windows) if (matched.contains(t.window)) {
            const bool hidden=locatePanel(canvas->m_doc,t.panel).where==PanelLocation::Where::Hidden;
            if (hidden) t.window->minimize(); else if (t.window->isMinimized()) t.window->unminimize();
        }
        canvas->m_nav.restore(navigation);
        canvas->applyRest();
        canvas->updateDock();
        for (const auto &t : canvas->m_windows) if (matched.contains(t.window)) {
            const bool hidden=t.window->isMinimized();
            if (hidden && !wasHidden.contains(t.window))
                canvas->startFlight(t.window,KrellisCanvas::FlightKind::Hide,before[t.window],canvas->chipRect(t.window));
            else if (!hidden)
                canvas->startFlight(t.window,wasHidden.contains(t.window)?KrellisCanvas::FlightKind::Restore:KrellisCanvas::FlightKind::Glide,
                    wasHidden.contains(t.window)?canvas->chipRect(t.window):before[t.window],{},wasHidden.contains(t.window)?0.1:1);
        }
        canvas->updateBar();
        canvas->schedule();
    }
    m_restoreComplete=true;
    m_saveTimer->start(1500);
}
} // namespace KWin
