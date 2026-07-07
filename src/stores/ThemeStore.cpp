#include "ThemeStore.h"
#include "SettingsStore.h"
#include "core/EnvConfig.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

constexpr const char *kBuiltinThemesDir = ":/ScootUI/assets/themes";
constexpr const char *kDefaultTheme = "default";

struct TokenDefault {
    const char *name;
    const char *dark;
    const char *light;
};

// The historical hardcoded palette. A theme file overrides any subset of
// these; missing tokens always inherit from here (never from the other
// variant), so a minimal theme JSON stays valid.
constexpr TokenDefault kTokenDefaults[] = {
    {"text",            "#FFFFFF",   "#000000"},
    {"textSecondary",   "#99FFFFFF", "#8A000000"},
    {"textTertiary",    "#4DFFFFFF", "#1F000000"},
    {"textHint",        "#8AFFFFFF", "#61000000"},
    {"background",      "#000000",   "#FFFFFF"},
    {"surface",         "#1E1E1E",   "#F5F5F5"},
    {"border",          "#1AFFFFFF", "#1F000000"},
    {"arcBackground",   "#424242",   "#E0E0E0"},
    {"powerBarBg",      "#424242",   "#E0E0E0"},
    {"powerZeroMark",   "#66FFFFFF", "#61000000"},
    {"accent",          "#2196F3",   "#2196F3"},
    {"speedFillHigh",   "#9C27B0",   "#9C27B0"},
    {"overspeedA",      "#9C27B0",   "#9C27B0"},
    {"overspeedB",      "#E91E63",   "#E91E63"},
    {"speedRegen",      "#4DFF0000", "#4DFF0000"},
    {"speedError",      "#F44336",   "#F44336"},
    {"speedTick",       "#80FFFFFF", "#1F000000"},
    {"speedLabelMajor", "#CCFFFFFF", "#4D000000"},
    // Material 800/900 — readable with white text in both variants
    {"statusSuccess",   "#2E7D32",   "#2E7D32"},
    {"statusWarning",   "#E65100",   "#E65100"},
    {"statusError",     "#C62828",   "#C62828"},
    {"statusNeutral",   "#424242",   "#424242"},
    {"statusInfo",      "#1565C0",   "#1565C0"},
};

void overlayPalette(QHash<QString, QColor> &palette, const QJsonObject &obj,
                    const QString &variant, const QString &source)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!palette.contains(it.key())) {
            qWarning() << "Theme" << source << variant << "has unknown token"
                       << it.key() << "- ignored";
            continue;
        }
        const QColor c = QColor::fromString(it.value().toString());
        if (!c.isValid()) {
            qWarning() << "Theme" << source << variant << "token" << it.key()
                       << "has invalid color" << it.value().toString() << "- keeping default";
            continue;
        }
        palette.insert(it.key(), c);
    }
}

} // namespace

const QHash<QString, QColor> &ThemeStore::defaults(bool dark)
{
    static const auto build = [](bool d) {
        QHash<QString, QColor> h;
        for (const auto &t : kTokenDefaults)
            h.insert(QLatin1String(t.name), QColor::fromString(QLatin1String(d ? t.dark : t.light)));
        return h;
    };
    static const QHash<QString, QColor> darkDefaults = build(true);
    static const QHash<QString, QColor> lightDefaults = build(false);
    return dark ? darkDefaults : lightDefaults;
}

ThemeStore::ThemeStore(SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_dark = defaults(true);
    m_light = defaults(false);

    connect(m_settings, &SettingsStore::themeChanged,
            this, &ThemeStore::onSettingsThemeChanged);
    connect(m_settings, &SettingsStore::colorThemeChanged,
            this, &ThemeStore::onColorThemeChanged);

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(200);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this]() {
        rescanThemes();
        // Re-apply the active theme so edits to its file repaint live
        applyColorTheme(m_requestedTheme);
    });

    m_dirPollTimer.setInterval(3000);
    connect(&m_dirPollTimer, &QTimer::timeout, this, [this]() {
        if (QDir(userThemesDir()).exists()) {
            updateWatchedPaths();  // arms the watcher, stops this poll
            m_reloadTimer.start();
        }
    });

    setupWatcher();

    rescanThemes();
    onSettingsThemeChanged();

    // Dev convenience: pick the startup theme from the environment (the
    // redis setting still applies when changed at runtime)
    m_requestedTheme = qEnvironmentVariable("SCOOTUI_COLOR_THEME");
    if (m_requestedTheme.isEmpty())
        m_requestedTheme = m_settings->colorTheme();
    applyColorTheme(m_requestedTheme);
}

QColor ThemeStore::pick(const char *tokenName) const
{
    const auto &palette = m_isDark ? m_dark : m_light;
    const auto it = palette.constFind(QLatin1String(tokenName));
    if (it != palette.constEnd())
        return *it;
    qWarning() << "ThemeStore: unknown token" << tokenName;
    return QColor(255, 0, 255); // magenta — make mistakes visible
}

QString ThemeStore::userThemesDir()
{
    return EnvConfig::dataDir() + QStringLiteral("/scootui/themes");
}

QString ThemeStore::resolveThemePath(const QString &name) const
{
    // User themes shadow built-ins of the same name so the community can
    // tweak shipped themes without a rebuild.
    const QString userPath = userThemesDir() + QLatin1Char('/') + name + QStringLiteral(".json");
    if (QFile::exists(userPath))
        return userPath;
    const QString builtinPath = QLatin1String(kBuiltinThemesDir) + QLatin1Char('/') + name + QStringLiteral(".json");
    if (QFile::exists(builtinPath))
        return builtinPath;
    return {};
}

void ThemeStore::rescanThemes()
{
    QStringList names{QLatin1String(kDefaultTheme)};
    const auto collect = [&names](const QString &dirPath) {
        const QDir dir(dirPath);
        for (const auto &entry : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            const QString name = QFileInfo(entry).completeBaseName();
            if (!names.contains(name))
                names.append(name);
        }
    };
    collect(QLatin1String(kBuiltinThemesDir));
    collect(userThemesDir());

    if (names != m_availableThemes) {
        m_availableThemes = names;
        emit availableThemesChanged();
    }
}

bool ThemeStore::loadThemeFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        *error = QStringLiteral("invalid JSON in %1: %2").arg(path, parseError.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    const int schemaVersion = root.value(QLatin1String("schemaVersion")).toInt(1);
    if (schemaVersion != 1) {
        *error = QStringLiteral("unsupported schemaVersion %1 in %2").arg(schemaVersion).arg(path);
        return false;
    }

    overlayPalette(m_dark, root.value(QLatin1String("dark")).toObject(),
                   QStringLiteral("dark"), path);
    overlayPalette(m_light, root.value(QLatin1String("light")).toObject(),
                   QStringLiteral("light"), path);

    // Optional per-theme map styles, relative to the theme file's directory
    const QJsonObject map = root.value(QLatin1String("map")).toObject();
    const QDir themeDir = QFileInfo(path).dir();
    const auto resolveStyle = [&themeDir](const QString &rel) -> QString {
        if (rel.isEmpty())
            return {};
        const QString abs = QDir::cleanPath(themeDir.filePath(rel));
        if (!QFile::exists(abs)) {
            qWarning() << "Theme map style not found:" << abs;
            return {};
        }
        return abs;
    };
    m_mapStyleDark = resolveStyle(map.value(QLatin1String("dark")).toString());
    m_mapStyleLight = resolveStyle(map.value(QLatin1String("light")).toString());
    return true;
}

void ThemeStore::applyColorTheme(const QString &name)
{
    const QString requested = name.isEmpty() ? QLatin1String(kDefaultTheme) : name;

    // Start from pristine defaults every time so switching away from a theme
    // never leaves its tokens behind.
    m_dark = defaults(true);
    m_light = defaults(false);
    m_mapStyleDark.clear();
    m_mapStyleLight.clear();
    m_activeThemeFile.clear();

    QString applied = requested;
    const QString path = resolveThemePath(requested);
    if (!path.isEmpty()) {
        QString error;
        if (loadThemeFile(path, &error)) {
            if (!path.startsWith(QLatin1Char(':')))
                m_activeThemeFile = path;
        } else {
            qWarning() << "Failed to load color theme" << requested << "-" << error;
            emit themeLoadFailed(QStringLiteral("Theme '%1' failed to load — using default").arg(requested));
            applied = QLatin1String(kDefaultTheme);
        }
    } else if (requested != QLatin1String(kDefaultTheme)) {
        qWarning() << "Color theme not found:" << requested;
        emit themeLoadFailed(QStringLiteral("Theme '%1' not found — using default").arg(requested));
        applied = QLatin1String(kDefaultTheme);
    }

    m_colorTheme = applied;
    updateWatchedPaths();
    emit themeChanged();
}

void ThemeStore::setupWatcher()
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        updateWatchedPaths();
        m_reloadTimer.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        // Editors replace files on save; re-arm the watch after the debounce
        m_reloadTimer.start();
    });
    updateWatchedPaths();
}

void ThemeStore::updateWatchedPaths()
{
    const QString themesDir = userThemesDir();

    if (QDir(themesDir).exists()) {
        if (!m_watcher.directories().contains(themesDir))
            m_watcher.addPath(themesDir);
        m_dirPollTimer.stop();
    } else {
        // inotify delivers no event when a filesystem is mounted over a
        // watched directory, so a parent watch never sees /data appear.
        // Poll until the themes dir exists (cheap stat while it doesn't).
        m_dirPollTimer.start();
    }

    if (!m_activeThemeFile.isEmpty() && QFile::exists(m_activeThemeFile)
        && !m_watcher.files().contains(m_activeThemeFile)) {
        m_watcher.addPath(m_activeThemeFile);
    }
}

void ThemeStore::onColorThemeChanged()
{
    m_requestedTheme = m_settings->colorTheme();
    applyColorTheme(m_requestedTheme);
}

void ThemeStore::onSettingsThemeChanged()
{
    const QString theme = m_settings->theme();
    bool autoMode = (theme == QLatin1String("auto"));
    bool dark = (theme != QLatin1String("light"));

    bool changed = false;
    if (autoMode != m_isAutoMode) {
        m_isAutoMode = autoMode;
        changed = true;
    }
    // In auto mode, don't change dark/light here - AutoThemeService handles it
    if (!autoMode && dark != m_isDark) {
        m_isDark = dark;
        changed = true;
    }
    if (changed)
        emit themeChanged();
}

void ThemeStore::setTheme(const QString &theme)
{
    bool dark = (theme != QLatin1String("light"));
    if (dark != m_isDark) {
        m_isDark = dark;
        emit themeChanged();
    }
}
