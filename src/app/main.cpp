#include "core/Logging.h"
#include "core/Version.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>

// ---------------------------------------------------------------------------
// ATK Player entry point.
//
// Deliberately thin: it configures the application object, starts logging and
// shows the window. Anything with behaviour belongs in a class that can be
// tested without a running event loop.
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    QCoreApplication::setApplicationName(QString::fromLatin1(atk::version::kApplicationName));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(atk::version::kString));
    QCoreApplication::setOrganizationName(QString::fromLatin1(atk::version::kOrganizationName));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(atk::version::kOrganizationDomain));

    atk::log::initialize();

    application.setStyleSheet(atk::ui::theme::styleSheet());

    atk::ui::MainWindow window;
    window.show();

    qCInfo(atk::log::app) << "Entering event loop";
    const int result = application.exec();
    qCInfo(atk::log::app) << "Exiting with code" << result;

    return result;
}
