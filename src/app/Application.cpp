#include "app/Application.h"

#include "core/Logging.h"
#include "media/MediaLibraryInfo.h"
#include "core/Version.h"
#include "ui/Theme.h"
#include "ui/IconProvider.h"
#include "ui/Resources.h"

#include <QFileOpenEvent>
#include <QStyleHints>

namespace atk::app {

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
}

Application::~Application() = default;

bool Application::initialize()
{
    ui::ensureResourcesInitialized();
    // Identity first: QStandardPaths derives the settings and data directories
    // from these, and PlatformInfo::applicationDataDirectory() is consulted by
    // the logging banner immediately afterwards.
    setApplicationName(QString::fromLatin1(version::kApplicationName));
    setApplicationVersion(versionString());
    setOrganizationName(QString::fromLatin1(version::kOrganizationName));
    setOrganizationDomain(QString::fromLatin1(version::kOrganizationDomain));
    setWindowIcon(ui::icons::applicationIcon());

    log::initialize();

    qCInfo(log::media).noquote() << "FFmpeg:" << media::libraryVersionSummary();

    parseArguments();

#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // The interface is dark; ask for the dark appearance so the native parts
    // macOS draws itself (title bar, window chrome) match it in Light Mode too.
    styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    setStyleSheet(ui::theme::styleSheet());

    qCInfo(log::app) << "Application initialised";
    return true;
}

void Application::parseArguments()
{
    const QStringList args = arguments();

    for (int i = 1; i < args.size(); ++i) {
        const QString& argument = args.at(i);

        if (argument == QLatin1String("--open") || argument == QLatin1String("-o")) {
            if (i + 1 < args.size()) {
                m_requestedMediaPath = args.at(++i);
            } else {
                qCWarning(log::app) << "--open was given without a file path";
            }
            continue;
        }

        // A bare path is accepted so file associations and drag-onto-exe work.
        if (!argument.startsWith(QLatin1Char('-')) && m_requestedMediaPath.isEmpty()) {
            m_requestedMediaPath = argument;
        }
    }

    if (!m_requestedMediaPath.isEmpty()) {
        qCInfo(log::app).noquote() << "Media requested on the command line:"
                                   << m_requestedMediaPath;
    }
}

bool Application::event(QEvent* event)
{
    if (event->type() == QEvent::FileOpen) {
        const QString path = static_cast<QFileOpenEvent*>(event)->file();
        if (!path.isEmpty()) {
            qCInfo(log::app).noquote() << "File open requested by the OS:" << path;
            emit fileOpenRequested(path);
        }
        return true;
    }
    return QApplication::event(event);
}

QString Application::versionString()
{
    return QString::fromLatin1(version::kString);
}

} // namespace atk::app
