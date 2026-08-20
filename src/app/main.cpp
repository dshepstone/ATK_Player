#include "app/Application.h"
#include "core/Logging.h"
#include "ui/MainWindow.h"

// ---------------------------------------------------------------------------
// ATK Player entry point.
//
// Deliberately thin. Startup logic lives in atk::app::Application, and
// everything with behaviour lives in a class that can be tested without a
// running event loop.
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    atk::app::Application application(argc, argv);

    if (!application.initialize()) {
        return 1;
    }

    atk::ui::MainWindow window;
    window.show();

    qCInfo(atk::log::app) << "Entering event loop";
    const int result = application.exec();
    qCInfo(atk::log::app) << "Exiting with code" << result;

    return result;
}
