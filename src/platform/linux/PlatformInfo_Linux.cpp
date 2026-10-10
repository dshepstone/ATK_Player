#include "platform/PlatformInfo.h"

#include "core/Logging.h"

#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

// Linux-only implementation. Not compiled on other platforms; provided now so
// the tree stays portable (milestone M8).

namespace atk::platform {

QString PlatformInfo::operatingSystemDescription()
{
    return QStringLiteral("%1 (%2 %3)")
        .arg(QSysInfo::prettyProductName(),
             QSysInfo::kernelType(),
             QSysInfo::kernelVersion());
}

QString PlatformInfo::applicationDataDirectory()
{
    // $XDG_DATA_HOME/ATK/ATK Player
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(path);
    return path;
}

bool PlatformInfo::setDisplaySleepInhibited(bool inhibited)
{
    // TODO(M8): org.freedesktop.ScreenSaver Inhibit/UnInhibit over D-Bus.
    Q_UNUSED(inhibited);
    qCDebug(log::platform) << "Display sleep inhibition not implemented on Linux yet";
    return false;
}

bool PlatformInfo::isFileOpenElsewhere(const QString& path)
{
    // Open files do not stop a file being replaced here, and a viewer that
    // has it open keeps reading its own copy safely.
    Q_UNUSED(path);
    return false;
}

} // namespace atk::platform
