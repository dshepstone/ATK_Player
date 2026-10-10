#include "platform/PlatformInfo.h"

#include "core/Logging.h"

#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

// macOS-only implementation. Not compiled on other platforms; provided now so
// the tree stays portable (milestone M7).

namespace atk::platform {

QString PlatformInfo::operatingSystemDescription()
{
    return QStringLiteral("%1 (%2)")
        .arg(QSysInfo::prettyProductName(), QSysInfo::productVersion());
}

QString PlatformInfo::applicationDataDirectory()
{
    // ~/Library/Application Support/ATK/ATK Player
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(path);
    return path;
}

bool PlatformInfo::setDisplaySleepInhibited(bool inhibited)
{
    // TODO(M7): IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, ...)
    Q_UNUSED(inhibited);
    qCDebug(log::platform) << "Display sleep inhibition not implemented on macOS yet";
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
