#include "platform/PlatformInfo.h"

#include "core/Logging.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QStandardPaths>
#include <QSysInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

bool PlatformInfo::isFileOpenElsewhere(const QString& path)
{
    // Ask for the file with no sharing at all: if anyone else holds it open,
    // Windows refuses with a sharing violation.
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    HANDLE handle = CreateFileW(native.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(handle);
    return false;
}

bool PlatformInfo::revealInFileManager(const QString& filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    }
    // "explorer /select,<path>" opens the folder with the file highlighted.
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   {QStringLiteral("/select,"), QDir::toNativeSeparators(info.absoluteFilePath())});
}

} // namespace atk::platform
