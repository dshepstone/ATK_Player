#include "app/Application.h"

#include "core/Logging.h"
#include "core/Version.h"
#include "ui/Theme.h"

namespace atk::app {

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
}

Application::~Application() = default;

bool Application::initialize()
{
    // Identity first: QStandardPaths derives the settings and data directories
    // from these, and PlatformInfo::applicationDataDirectory() is consulted by
    // the logging banner immediately afterwards.
    setApplicationName(QString::fromLatin1(version::kApplicationName));
    setApplicationVersion(versionString());
    setOrganizationName(QString::fromLatin1(version::kOrganizationName));
    setOrganizationDomain(QString::fromLatin1(version::kOrganizationDomain));

    log::initialize();

    setStyleSheet(ui::theme::styleSheet());

    qCInfo(log::app) << "Application initialised";
    return true;
}

QString Application::versionString()
{
    return QString::fromLatin1(version::kString);
}

} // namespace atk::app
