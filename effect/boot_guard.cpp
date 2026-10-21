#include "boot_guard.h"

#include "safemode.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtDebug>

namespace KWin {

namespace {

struct Stored {
    trellis::BootMarker marker;
    bool notified = false;
};

qint64 g_startedAtMs = 0; // this process's own marker; only it may flag itself healthy or clean

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString markerPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/kwin-trellis/boot.json");
}

// A missing or unreadable file is a clean default: the guard must never lock the effect out because of its own bookkeeping.
Stored load()
{
    Stored s;
    QFile f(markerPath());
    if (!f.open(QIODevice::ReadOnly)) return s;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) return s;
    s.marker.startedAtMs = qint64(o.value(QStringLiteral("startedAtMs")).toDouble());
    s.marker.cleanExit = o.value(QStringLiteral("cleanExit")).toBool(true);
    s.marker.survived = o.value(QStringLiteral("survived")).toBool(false);
    s.marker.strikes = o.value(QStringLiteral("strikes")).toInt(0);
    s.notified = o.value(QStringLiteral("notified")).toBool(false);
    return s;
}

void save(const Stored &s)
{
    QDir().mkpath(QFileInfo(markerPath()).absolutePath());
    QSaveFile f(markerPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    const QJsonObject o{{QStringLiteral("startedAtMs"), double(s.marker.startedAtMs)},
                        {QStringLiteral("cleanExit"), s.marker.cleanExit},
                        {QStringLiteral("survived"), s.marker.survived},
                        {QStringLiteral("strikes"), s.marker.strikes},
                        {QStringLiteral("notified"), s.notified}};
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.commit();
}

// The factory runs at login, possibly before the notification daemon is up and with no effect object to retry from, so a
// detached shell waits a little and uses notify-send instead of KNotification.
void notifyBlocked()
{
    QProcess::startDetached(QStringLiteral("sh"),
                            {QStringLiteral("-c"),
                             QStringLiteral("sleep 20; notify-send -a kwin-trellis -i dialog-warning 'Trellis was not loaded' "
                                            "'KWin kept crashing soon after loading the Trellis effect, so it stays off. "
                                            "Run tools/live.sh reset once the cause is fixed.'")});
}

} // namespace

bool trellisBootAllowed()
{
    Stored s = load();
    const trellis::BootDecision d = trellis::decideBoot(s.marker, nowMs());
    if (d.allow) return true;
    qWarning() << "Trellis not loaded: the last" << d.strikes << "starts ended in a crash within two minutes (marker" << markerPath() << ")";
    if (!s.notified) {
        s.notified = true;
        save(s);
        notifyBlocked();
    }
    return false;
}

void trellisBootStarted()
{
    const Stored prev = load();
    const trellis::BootDecision d = trellis::decideBoot(prev.marker, nowMs());
    g_startedAtMs = nowMs();
    Stored s;
    s.marker = {g_startedAtMs, false, false, d.strikes};
    save(s);
}

void trellisBootSurvived()
{
    Stored s = load();
    if (s.marker.startedAtMs != g_startedAtMs) return; // someone else's marker
    s.marker.survived = true;
    save(s);
}

void trellisBootClean()
{
    Stored s = load();
    if (s.marker.startedAtMs != g_startedAtMs) return;
    s.marker.cleanExit = true;
    s.marker.strikes = 0;
    save(s);
}

} // namespace KWin
