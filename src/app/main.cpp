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

    // Opened after show() so the window is already up and can display the
    // loading state rather than appearing only once decoding finishes.
    const QString requested = application.requestedMediaPath();
    if (!requested.isEmpty()) {
        if (requested.endsWith(QStringLiteral(".atkproj"), Qt::CaseInsensitive))
            window.openProjectFile(requested);
        else
            window.openMediaFile(requested);
    } else {
        window.reopenLastProjectIfEnabled();
    }

    qCInfo(atk::log::app) << "Entering event loop";
    const int result = application.exec();
    qCInfo(atk::log::app) << "Exiting with code" << result;

    return result;
}
