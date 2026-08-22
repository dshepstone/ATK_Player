#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>

class QSettings;

namespace atk::ui {

/// Single owner of machine-local application preferences. Media/session state
/// deliberately does not belong here; M3 project serialization owns that.
class ApplicationSettings {
public:
    ApplicationSettings();
    explicit ApplicationSettings(const QString& iniFilePath);
    ~ApplicationSettings();

    static constexpr bool defaultRestoreWindowLayout() { return true; }
    static constexpr bool defaultAudioScrubEnabled() { return true; }
    static constexpr bool defaultFrameStepAudioEnabled() { return false; }
    static constexpr bool defaultBookmarkSnapEnabled() { return true; }
    static constexpr double defaultVolume() { return 1.0; }
    static constexpr bool defaultMuted() { return false; }
    static constexpr bool defaultReopenLastProject() { return false; }
    static constexpr int maximumRecentProjects() { return 10; }
    static constexpr bool defaultApiEnabled() { return false; }
    static constexpr int defaultApiPort() { return 45571; }

    bool restoreWindowLayout() const;
    bool audioScrubEnabled() const;
    bool frameStepAudioEnabled() const;
    bool bookmarkSnapEnabled() const;
    double volume() const;
    bool muted() const;
    bool reopenLastProject() const;
    QStringList recentProjects() const;
    QString lastProjectPath() const;
    bool apiEnabled() const;
    int apiPort() const;

    void setRestoreWindowLayout(bool value);
    void setAudioScrubEnabled(bool value);
    void setFrameStepAudioEnabled(bool value);
    void setBookmarkSnapEnabled(bool value);
    void setVolume(double value);
    void setMuted(bool value);
    void setReopenLastProject(bool value);
    void addRecentProject(const QString& path);
    void clearRecentProjects();
    void setLastProjectPath(const QString& path);
    void setApiEnabled(bool value);
    void setApiPort(int value);

    QByteArray windowGeometry() const;
    QByteArray windowState() const;
    void setWindowGeometry(const QByteArray& value);
    void setWindowState(const QByteArray& value);

    /// Overrides are keyed by stable CommandDefinition::key. An empty value is
    /// an intentional cleared shortcut; absence means use the default.
    QHash<QString, QString> shortcutOverrides() const;
    bool setShortcutOverride(const QString& commandKey, const QString& portableShortcut);
    void resetShortcutOverride(const QString& commandKey);
    void resetAllShortcuts();

    void resetAll();
    void sync();

private:
    bool readBool(const char* key, bool fallback) const;
    std::unique_ptr<QSettings> m_settings;
};

} // namespace atk::ui
