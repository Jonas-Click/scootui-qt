#include "LayoutStore.h"
#include "SettingsStore.h"
#include "core/EnvConfig.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>

namespace {
constexpr const char *kDefaultLayout = "default";
constexpr int kSupportedApiVersion = 1;
constexpr const char *kManifestName = "layout.json";
} // namespace

LayoutStore::LayoutStore(SettingsStore *settings, QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_engine(engine)
{
    connect(m_settings, &SettingsStore::layoutChanged, this, [this]() {
        m_requestedLayout = m_settings->layout();
        applyLayout(m_requestedLayout);
    });

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(500);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this]() {
        rescanLayouts();
        reload();
    });

    m_dirPollTimer.setInterval(3000);
    connect(&m_dirPollTimer, &QTimer::timeout, this, [this]() {
        if (QDir(layoutsRootDir()).exists()) {
            updateWatchedPaths();  // arms the watcher, stops this poll
            m_reloadTimer.start();
        }
    });

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        updateWatchedPaths();
        m_reloadTimer.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        // Editors replace files on save; watches are re-armed after the debounce
        m_reloadTimer.start();
    });

    rescanLayouts();

    // Dev convenience: pick the startup layout from the environment (the
    // redis setting still applies when changed at runtime)
    m_requestedLayout = qEnvironmentVariable("SCOOTUI_LAYOUT");
    if (m_requestedLayout.isEmpty())
        m_requestedLayout = m_settings->layout();
    applyLayout(m_requestedLayout);
}

QString LayoutStore::layoutsRootDir()
{
    return EnvConfig::dataDir() + QStringLiteral("/scootui/layouts");
}

void LayoutStore::rescanLayouts()
{
    QStringList names{QLatin1String(kDefaultLayout)};
    const QDir root(layoutsRootDir());
    for (const auto &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (QFile::exists(root.filePath(entry + QLatin1Char('/') + QLatin1String(kManifestName)))
            && !names.contains(entry)) {
            names.append(entry);
        }
    }
    if (names != m_availableLayouts) {
        m_availableLayouts = names;
        emit availableLayoutsChanged();
    }
}

QUrl LayoutStore::resolvePack(const QString &name, QString *error) const
{
    const QDir packDir(layoutsRootDir() + QLatin1Char('/') + name);
    const QString manifestPath = packDir.filePath(QLatin1String(kManifestName));

    QFile f(manifestPath);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("layout '%1' not found").arg(name);
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        *error = QStringLiteral("layout '%1' has invalid manifest: %2")
                     .arg(name, parseError.errorString());
        return {};
    }
    const QJsonObject root = doc.object();

    const int apiVersion = root.value(QLatin1String("apiVersion")).toInt(1);
    if (apiVersion != kSupportedApiVersion) {
        *error = QStringLiteral("layout '%1' needs apiVersion %2 (this build supports %3)")
                     .arg(name).arg(apiVersion).arg(kSupportedApiVersion);
        return {};
    }

    const QString entry = root.value(QLatin1String("cluster"))
                              .toString(QStringLiteral("Cluster.qml"));
    const QString entryPath = QDir::cleanPath(packDir.filePath(entry));
    if (!entryPath.startsWith(QDir::cleanPath(packDir.absolutePath()))) {
        *error = QStringLiteral("layout '%1' entry point escapes the pack directory").arg(name);
        return {};
    }
    if (!QFile::exists(entryPath)) {
        *error = QStringLiteral("layout '%1' entry point missing: %2").arg(name, entry);
        return {};
    }
    return QUrl::fromLocalFile(entryPath);
}

void LayoutStore::applyLayout(const QString &name)
{
    const QString requested = name.isEmpty() ? QLatin1String(kDefaultLayout) : name;

    QUrl source;
    QString applied = requested;
    if (requested != QLatin1String(kDefaultLayout)) {
        QString error;
        source = resolvePack(requested, &error);
        // The engine caches compiled documents by URL and keeps entries that
        // are still instantiated, so a bare re-set of the same URL serves the
        // old compilation. A changing query makes each reload a fresh
        // document; relative URLs inside the pack are unaffected.
        if (!source.isEmpty() && m_reloadNonce > 0)
            source.setQuery(QStringLiteral("reload=%1").arg(m_reloadNonce));
        if (source.isEmpty()) {
            qWarning() << "LayoutStore:" << error;
            emit layoutLoadFailed(QStringLiteral("Layout '%1' failed to load — using default")
                                      .arg(requested));
            applied = QLatin1String(kDefaultLayout);
        }
    }

    if (source == m_clusterSource && applied == m_currentLayout)
        return;
    m_clusterSource = source;
    m_currentLayout = applied;
    updateWatchedPaths();
    qDebug() << "LayoutStore: layout" << m_currentLayout
             << (m_clusterSource.isEmpty() ? QStringLiteral("(builtin)") : m_clusterSource.toString());
    emit layoutChanged();
}

void LayoutStore::reload()
{
    if (m_clusterSource.isEmpty()) {
        m_engine->clearComponentCache();
        applyLayout(m_requestedLayout);
        return;
    }
    ++m_reloadNonce;
    m_clusterSource = QUrl();
    emit layoutChanged();
    QTimer::singleShot(0, this, [this]() {
        // The Loader has released the old item by now, so cached
        // compilations of the pack's files are evictable; the nonce on the
        // entry URL guarantees a fresh compile even where they are not.
        m_engine->clearComponentCache();
        applyLayout(m_requestedLayout);
    });
}

void LayoutStore::reportLoadError(const QString &errorString)
{
    qWarning() << "LayoutStore: QML load error:" << errorString;
    emit layoutLoadFailed(QStringLiteral("Layout '%1' failed to load — using default")
                              .arg(m_currentLayout));
}

void LayoutStore::updateWatchedPaths()
{
    const QString root = layoutsRootDir();

    if (QDir(root).exists()) {
        if (!m_watcher.directories().contains(root))
            m_watcher.addPath(root);
        m_dirPollTimer.stop();
    } else {
        // inotify delivers no event when a filesystem is mounted over a
        // watched directory, so a parent watch never sees /data appear.
        // Poll until the layouts dir exists (cheap stat while it doesn't).
        m_dirPollTimer.start();
    }

    // Watch the active pack for live edits: its directory, manifest and entry QML
    if (!m_clusterSource.isEmpty()) {
        const QString entryPath = m_clusterSource.toLocalFile();
        const QString packDir = QFileInfo(entryPath).absolutePath();
        const QString manifestPath = packDir + QLatin1Char('/') + QLatin1String(kManifestName);
        const QStringList paths{packDir, entryPath, manifestPath};
        for (const QString &p : paths) {
            if (QFile::exists(p)
                && !m_watcher.directories().contains(p) && !m_watcher.files().contains(p)) {
                m_watcher.addPath(p);
            }
        }
    }
}
