#include "ui/ApplicationSettings.h"

#include "core/Version.h"
#include "core/commands/CommandDefinitions.h"

#include <QKeySequence>
#include <QSettings>

#include <algorithm>

namespace atk::ui {
namespace {
constexpr auto kRestoreLayout = "ui/restoreWindowLayout";
constexpr auto kGeometry = "ui/mainWindowGeometry";
constexpr auto kWindowState = "ui/mainWindowState";
constexpr auto kAudioScrub = "review/audioScrub";
constexpr auto kFrameStepAudio = "review/frameStepAudio";
constexpr auto kBookmarkSnap = "review/bookmarkSnap";
constexpr auto kVolume = "review/volume";
constexpr auto kMuted = "review/muted";
constexpr auto kShortcutGroup = "shortcuts";
}

ApplicationSettings::ApplicationSettings()
    : m_settings(std::make_unique<QSettings>(
          QString::fromLatin1(version::kOrganizationName),
          QString::fromLatin1(version::kApplicationName)))
{
}

ApplicationSettings::ApplicationSettings(const QString& iniFilePath)
    : m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

ApplicationSettings::~ApplicationSettings() = default;

bool ApplicationSettings::readBool(const char* key, bool fallback) const
{
    const QVariant raw = m_settings->value(QString::fromLatin1(key));
    if (!raw.isValid()) return fallback;
    const QString text = raw.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) return true;
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) return false;
    return fallback;
}

bool ApplicationSettings::restoreWindowLayout() const { return readBool(kRestoreLayout, defaultRestoreWindowLayout()); }
bool ApplicationSettings::audioScrubEnabled() const { return readBool(kAudioScrub, defaultAudioScrubEnabled()); }
bool ApplicationSettings::frameStepAudioEnabled() const { return readBool(kFrameStepAudio, defaultFrameStepAudioEnabled()); }
bool ApplicationSettings::bookmarkSnapEnabled() const { return readBool(kBookmarkSnap, defaultBookmarkSnapEnabled()); }
bool ApplicationSettings::muted() const { return readBool(kMuted, defaultMuted()); }

double ApplicationSettings::volume() const
{
    bool ok = false;
    const double value = m_settings->value(QString::fromLatin1(kVolume), defaultVolume()).toDouble(&ok);
    return ok && value >= 0.0 && value <= 1.0 ? value : defaultVolume();
}

void ApplicationSettings::setRestoreWindowLayout(bool value) { m_settings->setValue(QString::fromLatin1(kRestoreLayout), value); }
void ApplicationSettings::setAudioScrubEnabled(bool value) { m_settings->setValue(QString::fromLatin1(kAudioScrub), value); }
void ApplicationSettings::setFrameStepAudioEnabled(bool value) { m_settings->setValue(QString::fromLatin1(kFrameStepAudio), value); }
void ApplicationSettings::setBookmarkSnapEnabled(bool value) { m_settings->setValue(QString::fromLatin1(kBookmarkSnap), value); }
void ApplicationSettings::setVolume(double value) { m_settings->setValue(QString::fromLatin1(kVolume), std::clamp(value, 0.0, 1.0)); }
void ApplicationSettings::setMuted(bool value) { m_settings->setValue(QString::fromLatin1(kMuted), value); }

QByteArray ApplicationSettings::windowGeometry() const { return m_settings->value(QString::fromLatin1(kGeometry)).toByteArray(); }
QByteArray ApplicationSettings::windowState() const { return m_settings->value(QString::fromLatin1(kWindowState)).toByteArray(); }
void ApplicationSettings::setWindowGeometry(const QByteArray& value) { m_settings->setValue(QString::fromLatin1(kGeometry), value); }
void ApplicationSettings::setWindowState(const QByteArray& value) { m_settings->setValue(QString::fromLatin1(kWindowState), value); }

QHash<QString, QString> ApplicationSettings::shortcutOverrides() const
{
    QHash<QString, QString> result;
    m_settings->beginGroup(QString::fromLatin1(kShortcutGroup));
    for (const QString& key : m_settings->childKeys()) {
        if (commands::find(key) == nullptr) continue;
        const QString stored = m_settings->value(key).toString();
        if (!stored.isEmpty() && QKeySequence::fromString(stored, QKeySequence::PortableText).isEmpty()) continue;
        result.insert(key, stored);
    }
    m_settings->endGroup();
    return result;
}

bool ApplicationSettings::setShortcutOverride(const QString& commandKey, const QString& portableShortcut)
{
    if (commands::find(commandKey) == nullptr) return false;
    if (!portableShortcut.isEmpty()
        && QKeySequence::fromString(portableShortcut, QKeySequence::PortableText).isEmpty()) return false;
    m_settings->setValue(QStringLiteral("%1/%2").arg(QString::fromLatin1(kShortcutGroup), commandKey), portableShortcut);
    return true;
}

void ApplicationSettings::resetShortcutOverride(const QString& commandKey)
{
    m_settings->remove(QStringLiteral("%1/%2").arg(QString::fromLatin1(kShortcutGroup), commandKey));
}

void ApplicationSettings::resetAllShortcuts() { m_settings->remove(QString::fromLatin1(kShortcutGroup)); }
void ApplicationSettings::resetAll() { m_settings->clear(); }
void ApplicationSettings::sync() { m_settings->sync(); }

} // namespace atk::ui
