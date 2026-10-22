#include "boot_guard.h"

#include "krellis_config.h"
#include "kwin_match.h"
#include "safemode.h"

#include <QDateTime>
#include <QCoreApplication>
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
    krellis::BootMarker marker;
    bool notified = false;
};

qint64 g_startedAtMs = 0; // this process's own marker; only it may flag itself healthy or clean

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString markerPath()
{
    return krellis::config::stateFile(QStringLiteral("boot.json"));
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
                             QStringLiteral("sleep 20; notify-send -a krellis -i dialog-warning 'Krellis was not loaded' "
                                            "'KWin kept crashing soon after loading the Krellis effect, so it stays off. "
                                            "Run tools/live.sh reset once the cause is fixed.'")});
}

// The KWin the plugin was compiled against, in a form the installer can read back out of the .so
// (`./install.sh --check`). `used` keeps the linker from dropping it.
[[maybe_unused]] __attribute__((used)) const char kBuiltFor[] = "krellis-built-for-kwin=" KRELLIS_KWIN_VERSION;

QString runningKWin()
{
    // KRELLIS_ASSUME_KWIN stands in for the running version in the nested tests.
    const QByteArray forced = qgetenv("KRELLIS_ASSUME_KWIN");
    return forced.isEmpty() ? QCoreApplication::applicationVersion() : QString::fromLocal8Bit(forced);
}

void notifyMismatch(const QString &built, const QString &running)
{
    const QString hint = QStringLiteral(KRELLIS_SOURCE_DIR);
    QProcess::startDetached(QStringLiteral("sh"),
                            {QStringLiteral("-c"),
                             QStringLiteral("sleep 20; notify-send -a krellis -i dialog-warning 'Krellis was not loaded' "
                                            "\"Krellis was built for KWin %1 but KWin %2 is running, so it stays off. "
                                            "Rebuild it: %3/install.sh\"")
                                 .arg(built, running, hint)});
}

} // namespace

bool krellisKWinMatches()
{
    const QString built = QStringLiteral(KRELLIS_KWIN_VERSION);
    const QString running = runningKWin();
    if (krellis::kwinVersionMatches(built.toStdString(), running.toStdString())) return true;
    if (qEnvironmentVariableIntValue("KRELLIS_ALLOW_KWIN_MISMATCH") == 1) {
        qWarning() << "Krellis built for KWin" << built << "is loading into KWin" << running << "(KRELLIS_ALLOW_KWIN_MISMATCH)";
        return true;
    }
    qWarning() << "Krellis not loaded: it was built for KWin" << built << "but KWin" << running << "is running. Rebuild with"
               << QStringLiteral(KRELLIS_SOURCE_DIR "/install.sh");
    // One notification per pair, so a session that queries support many times says it once.
    const QString pair = built + QStringLiteral("->") + running;
    QFile f(krellis::config::stateFile(QStringLiteral("kwin-mismatch")));
    if (f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).trimmed() == pair) return false;
    f.close();
    QDir().mkpath(QFileInfo(f.fileName()).absolutePath());
    QSaveFile out(f.fileName());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(pair.toUtf8());
        out.commit();
    }
    notifyMismatch(built, running);
    return false;
}

bool krellisBootAllowed()
{
    Stored s = load();
    const krellis::BootDecision d = krellis::decideBoot(s.marker, nowMs());
    if (d.allow) return true;
    qWarning() << "Krellis not loaded: the last" << d.strikes << "starts ended in a crash within two minutes (marker" << markerPath() << ")";
    if (!s.notified) {
        s.notified = true;
        save(s);
        notifyBlocked();
    }
    return false;
}

void krellisBootStarted()
{
    const Stored prev = load();
    const krellis::BootDecision d = krellis::decideBoot(prev.marker, nowMs());
    g_startedAtMs = nowMs();
    Stored s;
    s.marker = {g_startedAtMs, false, false, d.strikes};
    save(s);
}

void krellisBootSurvived()
{
    Stored s = load();
    if (s.marker.startedAtMs != g_startedAtMs) return; // someone else's marker
    s.marker.survived = true;
    save(s);
}

void krellisBootClean()
{
    Stored s = load();
    if (s.marker.startedAtMs != g_startedAtMs) return;
    s.marker.cleanExit = true;
    s.marker.strikes = 0;
    save(s);
}

} // namespace KWin
