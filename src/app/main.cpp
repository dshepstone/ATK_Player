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
    const auto openRequested = [&window](const QString& path) {
        if (path.endsWith(QStringLiteral(".atkproj"), Qt::CaseInsensitive))
            window.openProjectFile(path);
        else
            window.openMediaFile(path);
    };
    // macOS delivers Finder opens as events, including the one that launched
    // the app; they arrive once the event loop runs.
    QObject::connect(&application, &atk::app::Application::fileOpenRequested, &window, openRequested);

    const QString requested = application.requestedMediaPath();
    if (!requested.isEmpty()) {
        openRequested(requested);
    } else if (!window.reopenLastProjectIfEnabled()) {
        // Started on its own (no file from a double-click, Maya, Harmony or
        // --open): the Review Library, unless turned off in Preferences.
        window.showStartScreen();
    }

    // First launch after an install or upgrade: say thank you once.
    window.showWelcomeIfFirstRun();

    qCInfo(atk::log::app) << "Entering event loop";
    const int result = application.exec();
    qCInfo(atk::log::app) << "Exiting with code" << result;

    return result;
}
