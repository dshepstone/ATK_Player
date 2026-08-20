#pragma once

#include <QApplication>

namespace atk::app {

/// The ATK Player application object.
///
/// Owns process-wide setup that must happen before any window exists:
/// identity (used by QSettings paths), logging, and the visual theme.
///
/// It exists as a class rather than a run of statements in main() so that the
/// startup sequence has one place to live and one order. Adding
/// command-line parsing, a single-instance guard or a crash handler in a later
/// milestone means adding a step here, not lengthening main().
class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    /// Runs the startup sequence: identity, logging banner, theme.
    ///
    /// Separate from the constructor because it can fail, and because a
    /// QApplication must be fully constructed before anything it owns is
    /// touched. Returns false when startup cannot continue.
    bool initialize();

    /// Human-readable version, e.g. "0.2.0-dev".
    static QString versionString();

    /// Media file named on the command line, or an empty string.
    ///
    /// Supports both `ATKPlayer.exe <file>` and `ATKPlayer.exe --open <file>`.
    /// Deliberately hand-parsed rather than pulled in via QCommandLineParser:
    /// the point is file associations and test automation, not a CLI surface.
    QString requestedMediaPath() const { return m_requestedMediaPath; }

private:
    void parseArguments();

    QString m_requestedMediaPath;
};

} // namespace atk::app
