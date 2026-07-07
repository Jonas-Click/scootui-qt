#pragma once

#include <QObject>
#include <QColor>
#include <QHash>
#include <QStringList>
#include <QFileSystemWatcher>
#include <QTimer>

class SettingsStore;

// Design-token store. Token values come from a color-theme definition:
// built-in defaults (identical to the historical hardcoded palette) overlaid
// with the JSON theme selected via dashboard.color-theme. Themes ship in
// qrc:/ScootUI/assets/themes/ or live in <dataDir>/scootui/themes/*.json.
// dashboard.theme (auto|dark|light) keeps its meaning: it picks the variant
// of the active color theme.
class ThemeStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)
    Q_PROPERTY(QString themeName READ themeName NOTIFY themeChanged)
    Q_PROPERTY(bool isAutoMode READ isAutoMode NOTIFY themeChanged)
    Q_PROPERTY(QString colorTheme READ colorTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList availableThemes READ availableThemes NOTIFY availableThemesChanged)

    // Type scale (constant — not theme-dependent)
    Q_PROPERTY(qreal fontDisplay MEMBER s_fontDisplay CONSTANT)
    Q_PROPERTY(qreal fontPin MEMBER s_fontPin CONSTANT)
    Q_PROPERTY(qreal fontHero MEMBER s_fontHero CONSTANT)
    Q_PROPERTY(qreal fontXL MEMBER s_fontXL CONSTANT)
    Q_PROPERTY(qreal fontFeature MEMBER s_fontFeature CONSTANT)
    Q_PROPERTY(qreal fontInput MEMBER s_fontInput CONSTANT)
    Q_PROPERTY(qreal fontHeading MEMBER s_fontHeading CONSTANT)
    Q_PROPERTY(qreal fontTitle MEMBER s_fontTitle CONSTANT)
    Q_PROPERTY(qreal fontBody MEMBER s_fontBody CONSTANT)
    Q_PROPERTY(qreal fontCaption MEMBER s_fontCaption CONSTANT)
    Q_PROPERTY(qreal fontMicro MEMBER s_fontMicro CONSTANT)

    // Border radii (constant)
    Q_PROPERTY(qreal radiusBar MEMBER s_radiusBar CONSTANT)
    Q_PROPERTY(qreal radiusCard MEMBER s_radiusCard CONSTANT)
    Q_PROPERTY(qreal radiusModal MEMBER s_radiusModal CONSTANT)

    // Colors
    Q_PROPERTY(QColor textColor READ textColor NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor textTertiary READ textTertiary NOTIFY themeChanged)
    Q_PROPERTY(QColor textHint READ textHint NOTIFY themeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceColor READ surfaceColor NOTIFY themeChanged)
    Q_PROPERTY(QColor borderColor READ borderColor NOTIFY themeChanged)
    Q_PROPERTY(QColor arcBackground READ arcBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor powerBarBg READ powerBarBg NOTIFY themeChanged)
    Q_PROPERTY(QColor powerZeroMark READ powerZeroMark NOTIFY themeChanged)

    // Accent + speedometer palette
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor speedFillHigh READ speedFillHigh NOTIFY themeChanged)
    Q_PROPERTY(QColor overspeedA READ overspeedA NOTIFY themeChanged)
    Q_PROPERTY(QColor overspeedB READ overspeedB NOTIFY themeChanged)
    Q_PROPERTY(QColor speedRegen READ speedRegen NOTIFY themeChanged)
    Q_PROPERTY(QColor speedError READ speedError NOTIFY themeChanged)
    Q_PROPERTY(QColor speedTick READ speedTick NOTIFY themeChanged)
    Q_PROPERTY(QColor speedLabelMajor READ speedLabelMajor NOTIFY themeChanged)

    // Semantic status colors (deep shades, white text on top)
    Q_PROPERTY(QColor statusSuccess READ statusSuccess NOTIFY themeChanged)
    Q_PROPERTY(QColor statusWarning READ statusWarning NOTIFY themeChanged)
    Q_PROPERTY(QColor statusError   READ statusError   NOTIFY themeChanged)
    Q_PROPERTY(QColor statusNeutral READ statusNeutral NOTIFY themeChanged)
    Q_PROPERTY(QColor statusInfo    READ statusInfo    NOTIFY themeChanged)

public:
    explicit ThemeStore(SettingsStore *settings, QObject *parent = nullptr);

    bool isDark() const { return m_isDark; }
    bool isAutoMode() const { return m_isAutoMode; }
    QString themeName() const { return m_isDark ? QStringLiteral("dark") : QStringLiteral("light"); }
    QString colorTheme() const { return m_colorTheme; }
    QStringList availableThemes() const { return m_availableThemes; }

    QColor textColor() const { return pick("text"); }
    QColor textSecondary() const { return pick("textSecondary"); }
    QColor textTertiary() const { return pick("textTertiary"); }
    QColor textHint() const { return pick("textHint"); }
    QColor backgroundColor() const { return pick("background"); }
    QColor surfaceColor() const { return pick("surface"); }
    QColor borderColor() const { return pick("border"); }
    QColor arcBackground() const { return pick("arcBackground"); }
    QColor powerBarBg() const { return pick("powerBarBg"); }
    QColor powerZeroMark() const { return pick("powerZeroMark"); }

    QColor accent() const { return pick("accent"); }
    QColor speedFillHigh() const { return pick("speedFillHigh"); }
    QColor overspeedA() const { return pick("overspeedA"); }
    QColor overspeedB() const { return pick("overspeedB"); }
    QColor speedRegen() const { return pick("speedRegen"); }
    QColor speedError() const { return pick("speedError"); }
    QColor speedTick() const { return pick("speedTick"); }
    QColor speedLabelMajor() const { return pick("speedLabelMajor"); }

    QColor statusSuccess() const { return pick("statusSuccess"); }
    QColor statusWarning() const { return pick("statusWarning"); }
    QColor statusError()   const { return pick("statusError"); }
    QColor statusNeutral() const { return pick("statusNeutral"); }
    QColor statusInfo()    const { return pick("statusInfo"); }

    // Per-theme map style path (empty = use the built-in qrc style)
    QString mapStyle(bool dark) const { return dark ? m_mapStyleDark : m_mapStyleLight; }

    Q_INVOKABLE void setTheme(const QString &theme);
    // Token lookup by name — escape hatch for layout packs
    Q_INVOKABLE QColor token(const QString &name) const { return pick(name.toUtf8().constData()); }
    Q_INVOKABLE void rescanThemes();

signals:
    void themeChanged();
    void availableThemesChanged();
    void themeLoadFailed(const QString &message);

private slots:
    void onSettingsThemeChanged();
    void onColorThemeChanged();

private:
    QColor pick(const char *tokenName) const;
    void applyColorTheme(const QString &name);
    bool loadThemeFile(const QString &path, QString *error);
    QString resolveThemePath(const QString &name) const;
    void setupWatcher();
    void updateWatchedPaths();
    static QString userThemesDir();
    static const QHash<QString, QColor> &defaults(bool dark);

    SettingsStore *m_settings;
    bool m_isDark = true;
    bool m_isAutoMode = false;

    QString m_colorTheme = QStringLiteral("default");
    QString m_requestedTheme = QStringLiteral("default");
    QStringList m_availableThemes;
    QHash<QString, QColor> m_dark;
    QHash<QString, QColor> m_light;
    QString m_mapStyleDark;
    QString m_mapStyleLight;
    QString m_activeThemeFile;   // filesystem path of the active theme, if any

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QTimer m_dirPollTimer;   // waits for the user themes dir to appear (late /data mount)

    // Type scale constants
    static constexpr qreal s_fontDisplay = 96;
    static constexpr qreal s_fontPin     = 80;
    static constexpr qreal s_fontHero    = 64;
    static constexpr qreal s_fontXL      = 48;
    static constexpr qreal s_fontFeature = 36;
    static constexpr qreal s_fontInput   = 32;
    static constexpr qreal s_fontHeading = 28;
    static constexpr qreal s_fontTitle   = 20;
    static constexpr qreal s_fontBody    = 18;
    static constexpr qreal s_fontCaption = 14;
    static constexpr qreal s_fontMicro   = 10;

    // Border radii constants
    static constexpr qreal s_radiusBar   = 2;
    static constexpr qreal s_radiusCard  = 8;
    static constexpr qreal s_radiusModal = 16;
};
