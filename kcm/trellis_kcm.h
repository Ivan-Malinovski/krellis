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
    Q_PROPERTY(bool placeAtPointer READ placeAtPointer WRITE setPlaceAtPointer NOTIFY settingsChanged)
    Q_PROPERTY(int peek READ peek WRITE setPeek NOTIFY settingsChanged)
    Q_PROPERTY(bool map READ map WRITE setMap NOTIFY settingsChanged)
    Q_PROPERTY(bool mapPinned READ mapPinned WRITE setMapPinned NOTIFY settingsChanged)
    Q_PROPERTY(bool scratchpad READ scratchpad WRITE setScratchpad NOTIFY settingsChanged)
    Q_PROPERTY(bool glide READ glide WRITE setGlide NOTIFY settingsChanged)
    Q_PROPERTY(int landMs READ landMs WRITE setLandMs NOTIFY settingsChanged)
    Q_PROPERTY(RulesModel *rules READ rules CONSTANT)
public:
    TrellisKcm(QObject *parent, const KPluginMetaData &data);

    void load() override;
    void save() override;
    void defaults() override;

    bool floatFirst() const { return m_current.floatFirst; }
    bool placeAtPointer() const { return m_current.placeAtPointer; }
    int peek() const { return int(m_current.peek); }
    bool map() const { return m_current.map; }
    bool mapPinned() const { return m_current.mapPinned; }
    bool scratchpad() const { return m_current.scratchpad; }
    bool glide() const { return m_current.glide; }
    int landMs() const { return int(m_current.landMs); }
    void setFloatFirst(bool v) { edit([&] { m_current.floatFirst = v; }); }
    void setPlaceAtPointer(bool v) { edit([&] { m_current.placeAtPointer = v; }); }
    void setPeek(int v) { edit([&] { m_current.peek = v; }); }
    void setMap(bool v) { edit([&] { m_current.map = v; }); }
    void setMapPinned(bool v) { edit([&] { m_current.mapPinned = v; }); }
    void setScratchpad(bool v) { edit([&] { m_current.scratchpad = v; }); }
    void setGlide(bool v) { edit([&] { m_current.glide = v; }); }
    void setLandMs(int v) { edit([&] { m_current.landMs = v; }); }
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
