// The quick-jump palette, canvas side: the overlay scene, the ranking of what is typed, and the keys and pointer that
// drive it while it is open. The coordinator (multicanvas.cpp) builds the candidates, since it sees every output.
#include "krellis_effect.h"
#include "effect/effecthandler.h"
#include "effect/offscreenquickview.h"
#include "fuzzy.h"
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QQuickItem>
#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(krellisLog)

namespace KWin {
using namespace krellis;

void KrellisCanvas::ensurePaletteScene()
{
    if (m_palette) return;
    m_palette = makeScene(QStringLiteral("palette.qml"));
    if (!m_palette || !m_palette->rootItem()) {
        qCWarning(krellisLog) << "palette.qml failed to load";
        m_palette.reset();
        m_paletteOn = false;
        return;
    }
    m_palette->setGeometry(Rect(QRect(int(m_area.left()), int(m_area.top()), int(m_area.width()), int(m_area.height()))));
    m_palette->setVisible(!m_overlaysCovered);
    QMetaObject::invokeMethod(m_palette->rootItem(), "begin");
}

void KrellisCanvas::openPalette(std::vector<PaletteItem> items)
{
    if (m_unloading || items.empty()) return;
    m_paletteItems = std::move(items);
    m_paletteQuery.clear();
    m_paletteOn = true;
    ensurePaletteScene();
    if (!m_paletteOn) return;
    if (m_palette) QMetaObject::invokeMethod(m_palette->rootItem(), "begin"); // a scene still fading out starts empty again
    rankPalette();
    schedule();
}

void KrellisCanvas::closePalette()
{
    if (!m_paletteOn) return;
    m_paletteOn = false;
    m_paletteItems.clear();
    m_paletteRanked.clear();
    schedule();
}

void KrellisCanvas::rankPalette()
{
    std::vector<std::vector<FuzzyField>> candidates;
    candidates.reserve(m_paletteItems.size());
    for (const auto &item : m_paletteItems)
        candidates.push_back({{item.title.toStdString(), 1.0}, {item.app.toStdString(), 0.8}, {item.detail.toStdString(), 0.5}});
    m_paletteRanked = fuzzyRank(m_paletteQuery.toStdString(), candidates);
    m_paletteSel = m_paletteFirst = 0;
    m_paletteDirty = true;
}

void KrellisCanvas::paletteMove(int delta)
{
    const int n = int(m_paletteRanked.size());
    if (n == 0) return;
    m_paletteSel = ((m_paletteSel + delta) % n + n) % n;
    if (m_paletteSel < m_paletteFirst) m_paletteFirst = m_paletteSel;
    if (m_paletteSel >= m_paletteFirst + PALETTE_ROWS) m_paletteFirst = m_paletteSel - PALETTE_ROWS + 1;
    m_paletteDirty = true;
    schedule();
}

void KrellisCanvas::paletteGo(int rank)
{
    if (rank < 0 || size_t(rank) >= m_paletteRanked.size()) return;
    auto go = m_paletteItems[m_paletteRanked[size_t(rank)]].go;
    closePalette(); // first: the jump moves focus, and nothing should still be holding the keyboard
    if (go) go();
}

bool KrellisCanvas::paletteKey(const KeyboardKeyEvent &e)
{
    if (e.state == KeyboardKeyState::Released) {
        if (m_palette && m_paletteOn) { // a field wants its releases too
            QKeyEvent key(QEvent::KeyRelease, e.key, e.modifiers, e.nativeScanCode, e.nativeVirtualKey, 0, e.text, false);
            m_palette->forwardKeyEvent(&key);
        }
        return true;
    }
    const bool ctrl = e.modifiers.testFlag(Qt::ControlModifier);
    switch (e.key) {
    case Qt::Key_Escape: closePalette(); return true;
    case Qt::Key_Return: case Qt::Key_Enter: paletteGo(m_paletteSel); return true;
    case Qt::Key_Down: case Qt::Key_Tab: paletteMove(1); return true;
    case Qt::Key_Up: case Qt::Key_Backtab: paletteMove(-1); return true;
    case Qt::Key_PageDown: paletteMove(PALETTE_ROWS); return true;
    case Qt::Key_PageUp: paletteMove(-PALETTE_ROWS); return true;
    default: break;
    }
    if (ctrl && (e.key == Qt::Key_N || e.key == Qt::Key_J)) { paletteMove(1); return true; }
    if (ctrl && (e.key == Qt::Key_P || e.key == Qt::Key_K)) { paletteMove(-1); return true; }
    if (!m_palette || !m_palette->rootItem()) return true;
    QKeyEvent key(QEvent::KeyPress, e.key, e.modifiers, e.nativeScanCode, e.nativeVirtualKey, 0, e.text, e.state == KeyboardKeyState::Repeated);
    m_palette->forwardKeyEvent(&key);
    const QString typed = m_palette->rootItem()->property("query").toString();
    if (typed != m_paletteQuery) {
        m_paletteQuery = typed;
        rankPalette();
        schedule();
    }
    return true;
}

QString KrellisCanvas::paletteHit(QPointF p) const
{
    if (!m_paletteOn || !m_palette || !QRectF(QRect(m_palette->geometry())).contains(p)) return {};
    const QPointF local = p - QPointF(m_palette->geometry().x(), m_palette->geometry().y());
    QVariant out;
    QMetaObject::invokeMethod(m_palette->rootItem(), "hit", Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, local.x()), Q_ARG(QVariant, local.y()));
    return out.toString();
}

void KrellisCanvas::paletteHover(int row)
{
    const int rank = m_paletteFirst + row;
    if (row < 0 || rank == m_paletteSel || size_t(rank) >= m_paletteRanked.size()) return;
    m_paletteSel = rank;
    m_paletteDirty = true;
    schedule();
}

void KrellisCanvas::paletteScroll(double delta, bool wheel)
{
    // A wheel notch is one row; a touchpad sends a stream of small deltas, so those add up first.
    if (wheel) {
        paletteMove(delta > 0 ? 1 : -1);
        return;
    }
    m_paletteWheel += delta;
    while (std::abs(m_paletteWheel) >= 40) {
        paletteMove(m_paletteWheel > 0 ? 1 : -1);
        m_paletteWheel += m_paletteWheel > 0 ? -40 : 40;
    }
}

static QString badgeOf(const KrellisCanvas::PaletteItem &item)
{
    if (item.view) return QStringLiteral("V");
    const QString source = item.app.isEmpty() ? item.title : item.app;
    return source.isEmpty() ? QStringLiteral("?") : source.left(1).toUpper();
}

void KrellisCanvas::updatePalette(double dt)
{
    constexpr double IN_MS = 120, OUT_MS = 100;
    m_paletteBusy = false;
    if (!m_paletteOn && !m_palette) return;
    if (!m_palette) ensurePaletteScene();
    if (!m_palette) return;
    const double step = dt * 1000 / (m_paletteOn ? IN_MS : OUT_MS);
    const double before = m_paletteOpacity;
    m_paletteOpacity = std::clamp(m_paletteOpacity + (m_paletteOn ? step : -step), 0.0, 1.0);
    if (!m_paletteOn && m_paletteOpacity <= 0) {
        const Rect g = m_palette->geometry();
        m_palette.reset();
        effects->addRepaint(Region(g));
        return;
    }
    QQuickItem *item = m_palette->rootItem();
    if (m_paletteDirty) {
        QVariantList rows;
        const int end = std::min<int>(int(m_paletteRanked.size()), m_paletteFirst + PALETTE_ROWS);
        for (int rank = m_paletteFirst; rank < end; ++rank) {
            const auto &it = m_paletteItems[m_paletteRanked[size_t(rank)]];
            rows << QVariantMap{{QStringLiteral("title"), it.title.isEmpty() ? QStringLiteral("Untitled") : it.title},
                                {QStringLiteral("sub"), it.view ? QStringLiteral("Saved view") : it.app},
                                {QStringLiteral("detail"), it.detail},
                                {QStringLiteral("view"), it.view},
                                {QStringLiteral("badge"), badgeOf(it)}};
        }
        item->setProperty("rows", rows);
        item->setProperty("selected", m_paletteSel - m_paletteFirst);
        item->setProperty("first", m_paletteFirst);
        item->setProperty("total", int(m_paletteRanked.size()));
        m_paletteDirty = false;
        effects->addRepaint(Region(m_palette->geometry()));
    }
    if (m_paletteOpacity != before) {
        item->setProperty("paletteOpacity", m_paletteOpacity);
        effects->addRepaint(Region(m_palette->geometry()));
    }
    m_paletteBusy = !m_paletteOn || m_paletteOpacity < 1;
}

void KrellisCanvas::jumpTo(EffectWindow *w)
{
    Tracked *t = trackedFor(w);
    if (!t && stowedFor(w)) { // a tab behind another one: bring it forward first
        selectTab(stowedFor(w)->view, false);
        t = trackedFor(w);
    }
    if (!t) return;
    const std::string panel = t->panel;
    if (w->isMinimized()) w->unminimize();
    slideOutDrawer(panel);
    if (!floatScreen(panel)) m_nav.reveal(panel);
    effects->activateWindow(w);
    applyRest();
}

} // namespace KWin
