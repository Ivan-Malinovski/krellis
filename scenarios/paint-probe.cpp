// Test-only effect: inspect actual presented-frame rendering, without requesting repaints.
#include "effect/effect.h"
#include "effect/effecthandler.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "core/output.h"
#include "opengl/glframebuffer.h"
#include "opengl/glutils.h"
#include <QDBusConnection>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QElapsedTimer>
#include <array>
#include <cmath>
#include <vector>

namespace KWin {
class PaintProbe : public Effect {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.KrellisPaintProbe")
public:
    PaintProbe() {
        m_allowed = qEnvironmentVariable("WAYLAND_DISPLAY") != qEnvironmentVariable("NEST_SOCKET")
            && qEnvironmentVariable("NEST_SOCKET").startsWith("krellis-test-");
        QDBusConnection::sessionBus().registerObject("/KrellisPaintProbe", this, QDBusConnection::ExportAllSlots);
    }
    ~PaintProbe() override { QDBusConnection::sessionBus().unregisterObject("/KrellisPaintProbe"); }
    bool isActive() const override { return m_allowed; }
    int requestedEffectChainPosition() const override { return 0; }
    void paintScreen(const RenderTarget &target, const RenderViewport &viewport, int mask, const Region &region, LogicalOutput *screen) override {
        effects->paintScreen(target, viewport, mask, region, screen);
        if (!target.framebuffer()) return;
        GLFramebuffer::pushFramebuffer(target.framebuffer());
        if (m_watch) {
            // Count near-black samples over the whole frame: a black flash shows as a spike.
            int black = 0, total = 0;
            const QSize size = target.size();
            for (int j = 0; j < 12; ++j)
                for (int i = 0; i < 16; ++i, ++total) {
                    unsigned char px[4]{};
                    glReadPixels((i * 2 + 1) * size.width() / 32, (j * 2 + 1) * size.height() / 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    if (px[0] < 6 && px[1] < 6 && px[2] < 6) ++black;
                }
            ++m_watchFrames;
            const double fraction = double(black) / total;
            m_maxBlack = std::max(m_maxBlack, fraction);
            if (fraction > 0.02) ++m_blackFrames;
        }
        if (!m_enabled) { GLFramebuffer::popFramebuffer(); return; }
        ++m_frames;
        for (auto &probe : m_points) {
            if (screen && !QRectF(screen->geometry()).contains(probe.point)) continue;
            ++probe.samples;
            const auto point = viewport.mapToRenderTarget(probe.point);
            unsigned char pixel[4]{};
            glReadPixels(qRound(point.x()), target.size().height() - 1 - qRound(point.y()), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            const int r=pixel[0], g=pixel[1], b=pixel[2];
            probe.last = {r,g,b};
            if (m_history) probe.history.push_back({m_clock.elapsed(), r, g, b});
            if (probe.blink) {
                if (r>180 && g>180 && b<70) probe.seen |= 1;
                else if (r>180 && b>180 && g<70) probe.seen |= 2;
                else ++probe.bad;
            } else if (std::abs(r-probe.expected.red())>12 || std::abs(g-probe.expected.green())>12 || std::abs(b-probe.expected.blue())>12) ++probe.bad;
        }
        GLFramebuffer::popFramebuffer();
    }
public Q_SLOTS:
    bool configure(const QString &json) {
        if (!m_allowed) return false;
        m_points.clear(); m_frames=0;
        const auto rows = QJsonDocument::fromJson(json.toUtf8()).array();
        for (const auto row : rows) {
            const auto o=row.toObject();
            m_points.push_back({QPointF(o["x"].toDouble(),o["y"].toDouble()),QColor(o["color"].toString()),o["blink"].toBool()});
        }
        m_enabled=!m_points.empty(); return m_enabled;
    }
    // Every sampled pixel of every presented frame, with its time: lets a scenario see a colour come back.
    bool history(bool on) { m_history = on && m_allowed; m_clock.start(); for (auto &p : m_points) p.history.clear(); return m_history; }
    QString historyJson() const {
        QJsonArray points;
        for (const auto &p : m_points) {
            QJsonArray rows;
            for (const auto &h : p.history) rows.append(QJsonArray{double(h.t), h.r, h.g, h.b});
            points.append(rows);
        }
        return QString::fromUtf8(QJsonDocument(points).toJson(QJsonDocument::Compact));
    }
    bool watchBlack(bool on) { m_watch = on; m_watchFrames = m_blackFrames = 0; m_maxBlack = 0; return m_allowed && on; }
    QString blackStats() const {
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"frames", m_watchFrames}, {"blackFrames", m_blackFrames}, {"maxBlack", m_maxBlack}}).toJson(QJsonDocument::Compact));
    }
    QString stats() const {
        QJsonArray rows;
        for (const auto &p : m_points) rows.append(QJsonObject{{"bad",p.bad},{"seen",p.seen},{"samples",p.samples},{"last",QJsonArray{p.last[0],p.last[1],p.last[2]}}});
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"frames",m_frames},{"points",rows}}).toJson(QJsonDocument::Compact));
    }
private:
    struct Sample { qint64 t; int r, g, b; };
    struct Probe { QPointF point; QColor expected; bool blink; int bad=0,seen=0; std::array<int,3> last{}; int samples=0; std::vector<Sample> history; };
    std::vector<Probe> m_points;
    int m_frames=0;
    bool m_allowed=false,m_enabled=false,m_watch=false,m_history=false;
    QElapsedTimer m_clock;
    int m_watchFrames=0,m_blackFrames=0;
    double m_maxBlack=0;
};
KWIN_EFFECT_FACTORY(PaintProbe,"paint-probe.json")
}
#include "paint-probe.moc"
