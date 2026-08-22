#include "core/Logging.h"

#include "core/Version.h"
#include "platform/PlatformInfo.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QString>
#include <QSysInfo>

namespace atk::log {

Q_LOGGING_CATEGORY(app, "atk.app")
Q_LOGGING_CATEGORY(ui, "atk.ui")
Q_LOGGING_CATEGORY(media, "atk.media")
Q_LOGGING_CATEGORY(playback, "atk.playback")
Q_LOGGING_CATEGORY(timeline, "atk.timeline")
Q_LOGGING_CATEGORY(project, "atk.project")
Q_LOGGING_CATEGORY(exporting, "atk.export")
Q_LOGGING_CATEGORY(api, "atk.api")
Q_LOGGING_CATEGORY(platform, "atk.platform")

void initialize()
{
    // Timestamp, severity, category, message. Kept terse so the console stays
    // readable during development.
    qSetMessagePattern(QStringLiteral(
        "[%{time hh:mm:ss.zzz}] %{if-debug}DBG%{endif}%{if-info}INF%{endif}"
        "%{if-warning}WRN%{endif}%{if-critical}CRT%{endif}%{if-fatal}FTL%{endif}"
        " %{category} %{message}"));

    const QString buildType =
#ifdef QT_DEBUG
        QStringLiteral("Debug");
#else
        QStringLiteral("Release");
#endif

    qCInfo(app).noquote() << "----------------------------------------------";
    qCInfo(app).noquote() << QString::fromLatin1(version::kApplicationName)
                          << QString::fromLatin1(version::kString)
                          << QStringLiteral("(%1 build)").arg(buildType);
    qCInfo(app).noquote() << "Qt runtime  :" << QLibraryInfo::version().toString();
    qCInfo(app).noquote() << "OS          :" << atk::platform::PlatformInfo::operatingSystemDescription();
    qCInfo(app).noquote() << "Kernel      :" << QSysInfo::kernelType() << QSysInfo::kernelVersion();
    qCInfo(app).noquote() << "Architecture:" << QSysInfo::currentCpuArchitecture();
    qCInfo(app).noquote() << "----------------------------------------------";
}

} // namespace atk::log
