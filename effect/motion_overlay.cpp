#include "krellis_effect.h"
#include "effect/effecthandler.h"
#include "effect/offscreenquickview.h"
#include <QQuickItem>
#include <QRegion>
#include <QBuffer>
#include <cmath>

namespace KWin {
using namespace krellis;

void KrellisCanvas::updateMotionOverlay(double dt)
{
    const bool dragging = m_session && m_session->active();
    const double liftTarget = dragging ? 1 : 0;
    // Decoration feedback is independent of resize glides. landMs=0 together
    // with glide=false remains the existing way to request immediate motion.
    const bool feedback = m_glide || m_landMs > 0;
    if (dragging) m_liftPanel = m_session->lifted()->id;
    m_liftAmount = feedback ? m_liftAmount + (liftTarget - m_liftAmount) * (1 - std::exp(-dt / 0.035)) : liftTarget;
    if (std::abs(m_liftAmount - liftTarget) < 0.005) m_liftAmount = liftTarget;
    if (m_liftAmount == 0) m_liftPanel.clear();
    const double landing = feedback && !m_landingPanel.empty() ? std::clamp(1 - (m_nowMs - m_landingStart) / 180, 0.0, 1.0) : 0;
    const double rewind = feedback && !m_rewindPanels.empty() ? std::clamp(1 - (m_nowMs - m_rewindStart) / 220, 0.0, 1.0) : 0;
    if (landing == 0) m_landingPanel.clear();
    if (rewind == 0) m_rewindPanels.clear();
    const double keyMove = m_keyMoveHeld ? 1 : feedback
        ? std::clamp(1 - (m_nowMs - m_keyMoveReleased) / 180, 0.0, 1.0) : 0;
    if (keyMove == 0) m_keyMovePanel.clear();
    // Details appear as contents get smaller, rather than switching at a zoom threshold.
    const double spread = std::sqrt(double(std::max(size_t(1), leafIds(m_doc.root).size())));
    const double detail = dragging ? 0 : std::clamp((m_camera.value.w * spread - 1) / 0.8, 0.0, 1.0);
    // A steady amber outline on tiles that ask for attention; it eases in and out, then costs nothing while it holds.
    bool urgentSeen = false;
    for (const auto &t : m_windows)
        if (isUrgent(t.window) && !t.window->isMinimized() && !t.window->isHidden()) { urgentSeen = true; break; }
    const double urgentTarget = urgentSeen ? 1 : 0;
    m_urgentFade = feedback ? m_urgentFade + (urgentTarget - m_urgentFade) * (1 - std::exp(-dt / 0.08)) : urgentTarget;
    if (std::abs(m_urgentFade - urgentTarget) < 0.005) m_urgentFade = urgentTarget;
    m_motionBusy = m_liftAmount != liftTarget || landing > 0 || rewind > 0 || (!m_keyMoveHeld && keyMove > 0) || m_urgentFade != urgentTarget;
    if (!dragging && m_liftAmount == 0 && detail == 0 && landing == 0 && rewind == 0 && keyMove == 0 && m_urgentFade == 0) {
        if (m_motionOverlay) {
            repaintRegion(Region(m_motionOverlay->geometry()));
            m_motionOverlay.reset();
            m_motionIds.clear();
        }
        return;
    }
    if (!m_motionOverlay) {
        m_motionOverlay = makeScene(QStringLiteral("motion_overlay.qml"));
        if (!m_motionOverlay) return;
    }
    m_motionOverlay->setVisible(!m_overlaysCovered);
    m_motionOverlay->setGeometry(Rect(m_area.toAlignedRect()));
    auto *root = m_motionOverlay->rootItem();
    QStringList ids;
    for (const auto &t : m_windows) ids << QString::fromStdString(t.panel);
    if (ids != m_motionIds) {
        m_motionIds = ids;
        root->setProperty("panels", ids);
    }
    // visualRect searches the tracked windows. Cache each result for this frame;
    // otherwise testing every cover for every label repeats that search cubically.
    struct Painted { std::optional<TRect> rect; double opacity = 1; };
    std::map<EffectWindow *, Painted> painted;
    auto paintOf = [&](EffectWindow *window) -> const Painted & {
        auto [it, inserted] = painted.try_emplace(window);
        if (inserted) it->second.rect = visualRect(window, &it->second.opacity);
        return it->second;
    };
    const auto stack = effects->stackingOrder();
    for (size_t i = 0; i < m_windows.size(); ++i) {
        const auto &t = m_windows[i];
        const auto &paint = paintOf(t.window);
        const double opacity = paint.opacity;
        const auto rect = paint.rect.value_or(TRect{});
        const bool visible = paint.rect && !t.window->isMinimized() && !t.window->isHidden();
        const double flash = !visible ? 0 : t.panel == m_keyMovePanel ? keyMove : t.panel == m_landingPanel ? landing
            : m_rewindPanels.count(t.panel) ? rewind * 0.65 : 0;
        const double label = visible && !floatScreen(t.panel) ? detail * opacity * std::clamp((600 - rect.w) / 300, 0.0, 1.0) : 0;
        const double urgent = visible && isUrgent(t.window) ? m_urgentFade : 0;
        QVariantList pieces;
        if (label > 0 || flash > 0 || urgent > 0) {
            QRegion uncovered(QRectF(0, 0, rect.w, rect.h).toAlignedRect());
            bool above = false;
            for (auto *w : stack) {
                if (w == t.window) { above = true; continue; }
                if (!above || w->isDeleted() || w->isMinimized() || w->isHidden()) continue;
                if (const auto &cover = paintOf(w).rect)
                    uncovered -= QRectF(cover->x - rect.x, cover->y - rect.y, cover->w, cover->h).toAlignedRect();
                else if (!w->isDesktop()) {
                    const auto f = w->frameGeometry();
                    uncovered -= QRectF(f.x() - rect.x, f.y() - rect.y, f.width(), f.height()).toAlignedRect();
                }
            }
            for (const QRect &p : uncovered)
                pieces << QVariantMap{{"x", p.x()}, {"y", p.y()}, {"w", p.width()}, {"h", p.height()}};
        }
        auto &tile = m_tiles[t.panel];
        if (label > 0 && tile.icon.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (!t.window->icon().isNull()) t.window->icon().pixmap(48, 48).save(&buffer, "PNG");
            tile.icon = bytes.isEmpty() ? QStringLiteral("") : QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
        }
        QVariantMap data{{"x", rect.x - m_area.x()}, {"y", rect.y - m_area.y()}, {"w", rect.w}, {"h", rect.h},
            {"lift", visible && t.panel == m_liftPanel ? m_liftAmount : 0}, {"flash", flash}, {"urgent", urgent}, {"detail", label}, {"pieces", pieces},
            {"title", t.window->caption()}, {"icon", tile.icon}};
        QMetaObject::invokeMethod(root, "setPanel", Q_ARG(QVariant, int(i)), Q_ARG(QVariant, data));
    }
    QVariantList frames;
    if (detail > 0)
        for (const auto &f : m_nav.framings()) {
            std::optional<TRect> bounds;
            for (const auto &id : f.frame)
                if (const auto *r = m_rects.find(id)) {
                    const auto screen = panelAt(m_tween.apply(id, r->rect), m_camera.value);
                    if (!bounds) bounds = screen;
                    else {
                        const double x = std::min(bounds->x, screen.x), y = std::min(bounds->y, screen.y);
                        bounds = TRect{x, y, std::max(bounds->x + bounds->w, screen.x + screen.w) - x,
                                          std::max(bounds->y + bounds->h, screen.y + screen.h) - y};
                    }
                }
            if (!bounds) continue;
            const auto &r = *bounds;
            frames << QVariantMap{{"x", r.x - m_area.x() - 3}, {"y", r.y - m_area.y() - 3},
                {"w", r.w + 6}, {"h", r.h + 6}, {"amount", detail}, {"name", QString::fromStdString(f.name)}};
        }
    root->setProperty("frames", frames);
}
} // namespace KWin
