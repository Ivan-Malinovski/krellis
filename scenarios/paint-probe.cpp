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
#include <deque>

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
    ~PaintProbe() override {
        effects->makeOpenGLContextCurrent();
        for (const auto &query : m_gpuPending) glDeleteQueries(2, query.ids.data());
        QDBusConnection::sessionBus().unregisterObject("/KrellisPaintProbe");
    }
    bool isActive() const override { return m_allowed; }
    int requestedEffectChainPosition() const override { return 0; }
    void prePaintScreen(ScreenPrePaintData &data) override {
        if (m_timing) {
            m_frameStart = m_timingClock.nsecsElapsed();
            if (m_previousStart >= 0) m_intervals.append(double(m_frameStart - m_previousStart) / 1e6);
            m_previousStart = m_frameStart;
            m_frameStarts.append(double(m_frameStart) / 1e6);
            m_draws = m_shadowOnly = 0;
            m_frameArea = m_expandedArea = 0;
            m_paintMs = 0;
        }
        effects->prePaintScreen(data);
        if (m_timing) m_preMs = double(m_timingClock.nsecsElapsed()-m_frameStart)/1e6;
    }
    void postPaintScreen() override {
        const auto postStart = m_timing ? m_timingClock.nsecsElapsed() : 0;
        effects->postPaintScreen();
        if (m_timing) {
            m_stageTimes.append(QJsonObject{{"preMs",m_preMs},{"paintMs",m_paintMs},
                {"postMs",double(m_timingClock.nsecsElapsed()-postStart)/1e6}});
            m_frameTimes.append(double(m_timingClock.nsecsElapsed() - m_frameStart) / 1e6);
            m_drawStats.append(QJsonObject{{"draws", m_draws}, {"shadowOnly", m_shadowOnly},
                {"frameArea", m_frameArea}, {"expandedArea", m_expandedArea}});
        }
    }
    void drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &region, WindowPaintData &data) override {
        if (m_timing && !region.isEmpty()) {
            const auto frame = w->frameGeometry();
            const auto expanded = w->expandedGeometry();
            const QRectF shown(frame.x()+data.xTranslation(), frame.y()+data.yTranslation(),
                frame.width()*data.xScale(), frame.height()*data.yScale());
            const QRectF bounds(shown.x()+(expanded.x()-frame.x())*data.xScale(),
                shown.y()+(expanded.y()-frame.y())*data.yScale(),
                expanded.width()*data.xScale(), expanded.height()*data.yScale());
            const QRectF view(viewport.renderRect());
            const auto visible = shown.intersected(view), visibleExpanded = bounds.intersected(view);
            ++m_draws;
            if (visible.isEmpty() && !visibleExpanded.isEmpty()) ++m_shadowOnly;
            m_frameArea += visible.width()*visible.height();
            m_expandedArea += visibleExpanded.width()*visibleExpanded.height();
        }
        effects->drawWindow(target, viewport, w, mask, region, data);
    }
    void collectGpu() {
        while (!m_gpuPending.empty()) {
            const auto &query = m_gpuPending.front();
            GLuint ready = 0;
            glGetQueryObjectuiv(query.ids[1], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!ready) break; // never wait for the GPU to collect a timing sample
            GLuint64 start = 0, end = 0;
            glGetQueryObjectui64v(query.ids[0], GL_QUERY_RESULT, &start);
            glGetQueryObjectui64v(query.ids[1], GL_QUERY_RESULT, &end);
            if (query.generation == m_timingGeneration)
                m_gpuTimes.append(QJsonObject{{"startMs", query.startMs}, {"gpuMs", double(end-start)/1e6}});
            glDeleteQueries(2, query.ids.data());
            m_gpuPending.pop_front();
        }
    }
    void paintScreen(const RenderTarget &target, const RenderViewport &viewport, int mask, const Region &region, LogicalOutput *screen) override {
        collectGpu();
        const bool gpu = m_timing && qEnvironmentVariableIntValue("KRELLIS_PROBE_GPU_TIMING") == 1
            && EglContext::currentContext()->supportsTimerQueries();
        GpuQuery query;
        if (gpu) {
            query.startMs = double(m_frameStart)/1e6;
            query.generation = m_timingGeneration;
            glGenQueries(2, query.ids.data());
            glQueryCounter(query.ids[0], GL_TIMESTAMP);
        }
        const auto paintStart = m_timing ? m_timingClock.nsecsElapsed() : 0;
        effects->paintScreen(target, viewport, mask, region, screen);
        if (m_timing) m_paintMs += double(m_timingClock.nsecsElapsed()-paintStart)/1e6;
        if (gpu) {
            glQueryCounter(query.ids[1], GL_TIMESTAMP);
            m_gpuPending.push_back(query);
        }
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
    bool timing(bool on) {
        m_timing = on && m_allowed;
        if (m_timing) {
            m_frameTimes = {}; m_intervals = {}; m_frameStarts = {}; m_gpuTimes = {}; m_drawStats = {}; m_stageTimes = {};
            ++m_timingGeneration;
            m_previousStart = -1;
            m_timingClock.start();
        }
        return m_timing;
    }
    QString timingJson() const {
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"cpuMs", m_frameTimes}, {"intervalMs", m_intervals}, {"startMs", m_frameStarts}, {"gpu", m_gpuTimes}, {"drawStats", m_drawStats}, {"stages",m_stageTimes}}).toJson(QJsonDocument::Compact));
    }
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
    bool m_timing = false;
    QElapsedTimer m_timingClock;
    qint64 m_frameStart = 0, m_previousStart = -1;
    QJsonArray m_frameTimes, m_intervals, m_frameStarts, m_gpuTimes, m_drawStats, m_stageTimes;
    struct GpuQuery { std::array<GLuint, 2> ids{}; double startMs = 0; unsigned generation = 0; };
    std::deque<GpuQuery> m_gpuPending;
    unsigned m_timingGeneration = 0;
    int m_draws = 0, m_shadowOnly = 0;
    double m_frameArea = 0, m_expandedArea = 0, m_preMs = 0, m_paintMs = 0;
    int m_watchFrames=0,m_blackFrames=0;
    double m_maxBlack=0;
};
KWIN_EFFECT_FACTORY(PaintProbe,"paint-probe.json")
}
#include "paint-probe.moc"
