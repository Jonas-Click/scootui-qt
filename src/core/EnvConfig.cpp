#include "EnvConfig.h"
#include "AppConfig.h"

#include <QProcessEnvironment>
#include <QDebug>
#include <QDir>
#include <algorithm>

void EnvConfig::initialize()
{
    const auto env = QProcessEnvironment::systemEnvironment();

    // Settings file path
    const QString configPath = env.value(QStringLiteral("SCOOTUI_SETTINGS_PATH"));
    if (!configPath.isEmpty()) {
        AppConfig::settingsFilePath = configPath;
        qDebug() << "Using settings file from environment:" << configPath;
    }

    // Resolution: SCOOTUI_RESOLUTION=widthxheight
    const QString resStr = env.value(QStringLiteral("SCOOTUI_RESOLUTION"));
    if (!resStr.isEmpty()) {
        const auto parts = resStr.toLower().split(QLatin1Char('x'));
        if (parts.size() == 2) {
            bool okW = false, okH = false;
            const int w = parts[0].toInt(&okW);
            const int h = parts[1].toInt(&okH);
            if (okW && okH && w > 0 && h > 0) {
                m_resolution = QSize(w, h);
                const qreal defaultMin = std::min(defaultWidth, defaultHeight);
                const qreal newMin = std::min(w, h);
                m_scaleFactor = newMin / defaultMin;
                qDebug() << "Resolution:" << m_resolution << "Scale:" << m_scaleFactor;
            } else {
                qWarning() << "Invalid SCOOTUI_RESOLUTION:" << resStr;
            }
        }
    }

    // User content root: SCOOTUI_DATA_DIR (themes, layouts)
    const QString dataDirStr = env.value(QStringLiteral("SCOOTUI_DATA_DIR"));
    if (!dataDirStr.isEmpty()) {
        m_dataDir = dataDirStr;
        qDebug() << "Data dir:" << m_dataDir;
    }

    // Filesystem layout packs aren't AOT-compiled like the qrc QML; persist
    // the JIT cache so the DBC pays the compile once, not every boot.
    if (!env.contains(QStringLiteral("QML_DISK_CACHE_PATH"))) {
        const QString cacheDir = m_dataDir + QStringLiteral("/scootui/.qmlcache");
        if (QDir(m_dataDir).exists() && QDir().mkpath(cacheDir))
            qputenv("QML_DISK_CACHE_PATH", cacheDir.toUtf8());
    }

    // Redis host: SCOOTUI_REDIS_HOST=host:port or just host
    const QString redisStr = env.value(QStringLiteral("SCOOTUI_REDIS_HOST"));
    if (!redisStr.isEmpty()) {
        if (redisStr.contains(QLatin1Char(':'))) {
            const auto parts = redisStr.split(QLatin1Char(':'));
            m_redisHost = parts[0];
            bool ok = false;
            const int port = parts[1].toInt(&ok);
            if (ok) m_redisPort = port;
        } else {
            m_redisHost = redisStr;
        }
        qDebug() << "Redis:" << m_redisHost << ":" << m_redisPort;
    }
}
