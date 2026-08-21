#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

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

    bool restoreWindowLayout() const;
    bool audioScrubEnabled() const;
    bool frameStepAudioEnabled() const;
    bool bookmarkSnapEnabled() const;
    double volume() const;
    bool muted() const;

    void setRestoreWindowLayout(bool value);
    void setAudioScrubEnabled(bool value);
    void setFrameStepAudioEnabled(bool value);
    void setBookmarkSnapEnabled(bool value);
    void setVolume(double value);
    void setMuted(bool value);

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
