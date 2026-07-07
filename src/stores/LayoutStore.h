#pragma once

#include <QObject>
#include <QUrl>
#include <QStringList>
#include <QFileSystemWatcher>
#include <QTimer>

class SettingsStore;
class QQmlEngine;

// Layout packs: user-authored QML cluster designs loaded from
// <dataDir>/scootui/layouts/<name>/ (manifest layout.json + a cluster QML
// entry point). Selected via dashboard.layout; "default" or any load
// failure falls back to the built-in ClusterScreen. The active pack is
// watched so edits hot-reload while the dashboard runs.
class LayoutStore : public QObject
{
    Q_OBJECT
    // file:// URL of the custom cluster QML; empty => use the built-in screen
    Q_PROPERTY(QUrl clusterSource READ clusterSource NOTIFY layoutChanged)
    Q_PROPERTY(QString currentLayout READ currentLayout NOTIFY layoutChanged)
    Q_PROPERTY(QStringList availableLayouts READ availableLayouts NOTIFY availableLayoutsChanged)

public:
    explicit LayoutStore(SettingsStore *settings, QQmlEngine *engine, QObject *parent = nullptr);

    QUrl clusterSource() const { return m_clusterSource; }
    QString currentLayout() const { return m_currentLayout; }
    QStringList availableLayouts() const { return m_availableLayouts; }

    // Called from QML when the Loader errors on the custom layout
    Q_INVOKABLE void reportLoadError(const QString &errorString);
    // Clear the QML component cache and re-instantiate the active layout
    Q_INVOKABLE void reload();
    Q_INVOKABLE void rescanLayouts();

signals:
    void layoutChanged();
    void availableLayoutsChanged();
    void layoutLoadFailed(const QString &message);

private:
    void applyLayout(const QString &name);
    QUrl resolvePack(const QString &name, QString *error) const;
    void updateWatchedPaths();
    static QString layoutsRootDir();

    SettingsStore *m_settings;
    QQmlEngine *m_engine;
    QUrl m_clusterSource;
    QString m_currentLayout = QStringLiteral("default");
    QString m_requestedLayout = QStringLiteral("default");
    QStringList m_availableLayouts;

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QTimer m_dirPollTimer;   // waits for the layouts dir to appear (late /data mount)
    int m_reloadNonce = 0;   // cache-busts the entry URL on hot reload
};
