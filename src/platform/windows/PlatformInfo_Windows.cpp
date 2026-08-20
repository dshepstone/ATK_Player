#include "platform/PlatformInfo.h"

#include "core/Logging.h"

#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

// Windows-only implementation. Anything requiring <windows.h> belongs in this
// file, never in core or UI code.

namespace atk::platform {

QString PlatformInfo::operatingSystemDescription()
{
    return QStringLiteral("%1 (%2)")
        .arg(QSysInfo::prettyProductName(), QSysInfo::productVersion());
}

QString PlatformInfo::applicationDataDirectory()
{
    // %LOCALAPPDATA%/ATK/ATK Player
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(path);
    return path;
}

bool PlatformInfo::setDisplaySleepInhibited(bool inhibited)
{
    // TODO(M1): call SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED)
    //           while playing, and restore ES_CONTINUOUS when paused/stopped.
    Q_UNUSED(inhibited);
    qCDebug(log::platform) << "Display sleep inhibition not implemented on Windows yet";
    return false;
}

} // namespace atk::platform
