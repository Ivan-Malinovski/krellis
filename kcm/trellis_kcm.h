#pragma once
#include <KQuickConfigModule>
#include <QAbstractListModel>
#include "trellis_config.h"

// Per-application rules as a list model the QML edits in place.
class RulesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { App = Qt::UserRole + 1, Placement, Scaling, MinWidth, MinHeight, AllowSide, AllowStage, AllowFloating, Singleton, Closable, Scratchpad };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rules.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<trellis::config::AppRule> &rules() const { return m_rules; }
    void setRules(QList<trellis::config::AppRule> rules);
    Q_INVOKABLE void add(const QString &app);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void set(int row, const QString &role, const QVariant &value);
    Q_INVOKABLE bool contains(const QString &app) const;

Q_SIGNALS:
    void edited();

private:
    QList<trellis::config::AppRule> m_rules;
};

class TrellisKcm : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(bool floatFirst READ floatFirst WRITE setFloatFirst NOTIFY settingsChanged)
    Q_PROPERTY(int spawnTarget READ spawnTarget WRITE setSpawnTarget NOTIFY settingsChanged) // 0 pointer, 1 focused, 2 end
    Q_PROPERTY(int peek READ peek WRITE setPeek NOTIFY settingsChanged)
    Q_PROPERTY(bool map READ map WRITE setMap NOTIFY settingsChanged)
    Q_PROPERTY(bool mapPinned READ mapPinned WRITE setMapPinned NOTIFY settingsChanged)
    Q_PROPERTY(bool scratchpad READ scratchpad WRITE setScratchpad NOTIFY settingsChanged)
    Q_PROPERTY(bool glide READ glide WRITE setGlide NOTIFY settingsChanged)
    Q_PROPERTY(int landMs READ landMs WRITE setLandMs NOTIFY settingsChanged)
    Q_PROPERTY(int iconBelow READ iconBelow WRITE setIconBelow NOTIFY settingsChanged)
    Q_PROPERTY(bool followTheme READ followTheme WRITE setFollowTheme NOTIFY settingsChanged)
    Q_PROPERTY(bool toasts READ toasts WRITE setToasts NOTIFY settingsChanged)
    Q_PROPERTY(bool flyTo READ flyTo WRITE setFlyTo NOTIFY settingsChanged)
    Q_PROPERTY(bool whichKey READ whichKey WRITE setWhichKey NOTIFY settingsChanged)
    Q_PROPERTY(int whichKeyDelayMs READ whichKeyDelayMs WRITE setWhichKeyDelayMs NOTIFY settingsChanged)
    Q_PROPERTY(RulesModel *rules READ rules CONSTANT)
public:
    TrellisKcm(QObject *parent, const KPluginMetaData &data);

    void load() override;
    void save() override;
    void defaults() override;

    bool floatFirst() const { return m_current.floatFirst; }
    int spawnTarget() const { return int(m_current.spawnTarget); }
    int peek() const { return int(m_current.peek); }
    bool map() const { return m_current.map; }
    bool mapPinned() const { return m_current.mapPinned; }
    bool scratchpad() const { return m_current.scratchpad; }
    bool glide() const { return m_current.glide; }
    int landMs() const { return int(m_current.landMs); }
    int iconBelow() const { return m_current.iconBelow; }
    bool followTheme() const { return m_current.followTheme; }
    bool toasts() const { return m_current.toasts; }
    bool flyTo() const { return m_current.flyTo; }
    bool whichKey() const { return m_current.whichKey; }
    int whichKeyDelayMs() const { return m_current.whichKeyDelayMs; }
    void setFloatFirst(bool v) { edit([&] { m_current.floatFirst = v; }); }
    void setSpawnTarget(int v) { edit([&] { m_current.spawnTarget = trellis::config::SpawnTarget(std::clamp(v, 0, 2)); }); }
    void setPeek(int v) { edit([&] { m_current.peek = v; }); }
    void setMap(bool v) { edit([&] { m_current.map = v; }); }
    void setMapPinned(bool v) { edit([&] { m_current.mapPinned = v; }); }
    void setScratchpad(bool v) { edit([&] { m_current.scratchpad = v; }); }
    void setGlide(bool v) { edit([&] { m_current.glide = v; }); }
    void setLandMs(int v) { edit([&] { m_current.landMs = v; }); }
    void setIconBelow(int v) { edit([&] { m_current.iconBelow = v; }); }
    void setFollowTheme(bool v) { edit([&] { m_current.followTheme = v; }); }
    void setToasts(bool v) { edit([&] { m_current.toasts = v; }); }
    void setFlyTo(bool v) { edit([&] { m_current.flyTo = v; }); }
    void setWhichKey(bool v) { edit([&] { m_current.whichKey = v; }); }
    void setWhichKeyDelayMs(int v) { edit([&] { m_current.whichKeyDelayMs = std::clamp(v, 100, 2000); }); }
    RulesModel *rules() { return &m_rules; }

Q_SIGNALS:
    void settingsChanged();

private:
    template<class F> void edit(F &&apply) { apply(); settingsChanged(); refreshState(); }
    void refreshState();

    trellis::config::Settings m_current, m_saved;
    QList<trellis::config::AppRule> m_savedRules;
    RulesModel m_rules;
};
