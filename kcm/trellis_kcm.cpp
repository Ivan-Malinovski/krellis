#include "trellis_kcm.h"
#include <KLocalizedString>
#include <KPluginFactory>
#include <algorithm>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

using namespace trellis::config;

QHash<int, QByteArray> RulesModel::roleNames() const
{
    return {{App, "app"}, {Placement, "placement"}, {Scaling, "scaling"}, {MinWidth, "minWidth"}, {MinHeight, "minHeight"},
            {AllowSide, "allowSide"}, {AllowStage, "allowStage"}, {AllowFloating, "allowFloating"}, {Singleton, "singleton"}, {Closable, "closable"}};
}

QVariant RulesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rules.size()) return {};
    const AppRule &r = m_rules[index.row()];
    switch (role) {
    case App: return r.app;
    case Placement: return r.placement;
    case Scaling: return r.scaling;
    case MinWidth: return r.minWidth;
    case MinHeight: return r.minHeight;
    case AllowSide: return r.allowSide;
    case AllowStage: return r.allowStage;
    case AllowFloating: return r.allowFloating;
    case Singleton: return r.singleton;
    case Closable: return r.closable;
    }
    return {};
}

bool RulesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_rules.size()) return false;
    AppRule &r = m_rules[index.row()];
    switch (role) {
    case App: r.app = value.toString(); break;
    case Placement: r.placement = value.toString(); break;
    case Scaling: r.scaling = value.toString(); break;
    case MinWidth: r.minWidth = value.toInt(); break;
    case MinHeight: r.minHeight = value.toInt(); break;
    case AllowSide: r.allowSide = value.toBool(); break;
    case AllowStage: r.allowStage = value.toBool(); break;
    case AllowFloating: r.allowFloating = value.toBool(); break;
    case Singleton: r.singleton = value.toBool(); break;
    case Closable: r.closable = value.toBool(); break;
    default: return false;
    }
    Q_EMIT dataChanged(index, index, {role});
    Q_EMIT edited();
    return true;
}

void RulesModel::setRules(QList<AppRule> rules)
{
    beginResetModel();
    m_rules = std::move(rules);
    endResetModel();
}

void RulesModel::add(const QString &app)
{
    AppRule r;
    r.app = app.trimmed();
    if (r.app.isEmpty() || contains(r.app)) return;
    beginInsertRows({}, m_rules.size(), m_rules.size());
    m_rules.append(r);
    endInsertRows();
    Q_EMIT edited();
}

void RulesModel::remove(int row)
{
    if (row < 0 || row >= m_rules.size()) return;
    beginRemoveRows({}, row, row);
    m_rules.removeAt(row);
    endRemoveRows();
    Q_EMIT edited();
}

void RulesModel::set(int row, const QString &role, const QVariant &value)
{
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it)
        if (it.value() == role.toUtf8()) {
            setData(index(row), value, it.key());
            return;
        }
}

bool RulesModel::contains(const QString &app) const
{
    return std::any_of(m_rules.begin(), m_rules.end(), [&](const AppRule &r) { return r.app == app.trimmed(); });
}

TrellisKcm::TrellisKcm(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
{
    setButtons(Apply | Default);
    connect(&m_rules, &RulesModel::edited, this, &TrellisKcm::refreshState);
}

void TrellisKcm::load()
{
    m_current = m_saved = trellis::config::load();
    m_savedRules = loadRules();
    m_rules.setRules(m_savedRules);
    Q_EMIT settingsChanged();
    refreshState();
}

void TrellisKcm::save()
{
    trellis::config::save(m_current);
    saveRules(m_rules.rules());
    m_saved = m_current;
    m_savedRules = m_rules.rules();
    refreshState();
    // Tell a running effect to re-read the file; with the effect off nobody answers and that is fine.
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Trellis"),
        QStringLiteral("org.kde.kwin.KWin.TrellisEffect"), QStringLiteral("reloadSettings")));
}

void TrellisKcm::defaults()
{
    m_current = Settings{};
    m_rules.setRules({});
    Q_EMIT settingsChanged();
    refreshState();
}

void TrellisKcm::refreshState()
{
    setNeedsSave(!(m_current == m_saved) || m_rules.rules() != m_savedRules);
    setRepresentsDefaults(m_current == Settings{} && m_rules.rules().isEmpty());
}

K_PLUGIN_CLASS_WITH_JSON(TrellisKcm, "kcm_trellis.json")

#include "trellis_kcm.moc"
