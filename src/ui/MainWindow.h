#pragma once

#include "core/commands/CommandDefinitions.h"
#include "core/commands/CommandId.h"
#include "media/MediaMetadata.h"
#include "playback/PlaybackController.h"

#include <QMainWindow>

#include <memory>

class QDockWidget;
class QLabel;
class QSpinBox;
class QSlider;

namespace atk::api { class ApiServer; }
namespace atk::playback { class CompareSession; }
namespace atk::project { class Project; }
namespace atk::timeline { class TimelineModel; }

namespace atk::ui {

class CommandRegistry;
class ApplicationSettings;
class BookmarkPanel;
class PreferencesDialog;
class SourcesPanel;
class StatusInfoBar;
class TimelineWidget;
class TimelineRangeSlider;
class TransportControls;
class ViewerWidget;

/// The application window.
///
/// It owns the session objects -- timeline model, playback controller, project,
/// compare session and API server -- and wires the widgets to them. It contains
/// no playback or decoding logic itself: it translates commands into calls on
/// PlaybackController and lets the widgets observe the models.
///
/// Command handling is centralised in onCommand(). No widget installs a key
/// handler; anything that a key can do, a menu entry and the API can do too.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    /// Isolated settings backend for deterministic UI tests.
    explicit MainWindow(const QString& settingsIniPath, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void buildModels();
    void buildWidgets();
    void buildMenus();
    void connectSignals();

    /// Single entry point for every command in the application.
    void onCommand(commands::CommandId id, bool checked);

    /// Shows the file dialog and opens what the user picked.
    void openMediaDialog();

public:
    /// Opens a file directly, bypassing the dialog. Used by the command line
    /// and, later, by file associations and the external API.
    void openMediaFile(const QString& filePath);
    playback::PlaybackController* playbackController() const { return m_playback.get(); }

private:
    void onPlayerStateChanged(playback::PlayerState state);
    void onMediaOpened(const media::MediaMetadata& metadata);
    void onMediaError(const QString& message);
    void updateTransportEnabled();
    void buildAudioControls();
    void installPlaceholderTimeline();

    /// Phase 0 helper: reports a command that exists but has no behaviour yet.
    void reportNotImplemented(commands::CommandId id);

    void updateWindowTitle();
    void activateBookmark(quint64 id);
    void openPreferences();
    void applyPreferences(const PreferencesDialog& dialog);
    void restoreApplicationLayout();
    void saveApplicationLayout();


    std::unique_ptr<ApplicationSettings> m_settings;

    // Session models. Declared before the widgets that observe them so
    // destruction runs in the safe order.
    std::unique_ptr<timeline::TimelineModel> m_timeline;
    std::unique_ptr<playback::PlaybackController> m_playback;
    std::unique_ptr<project::Project> m_project;
    std::unique_ptr<playback::CompareSession> m_compare;
    std::unique_ptr<api::ApiServer> m_apiServer;

    CommandRegistry* m_commands = nullptr;
    BookmarkPanel* m_bookmarks = nullptr;

    ViewerWidget* m_viewer = nullptr;
    TimelineWidget* m_timelineWidget = nullptr;
    TimelineRangeSlider* m_timelineRangeSlider = nullptr;
    QSpinBox* m_reviewStartFrame = nullptr;
    QSpinBox* m_reviewEndFrame = nullptr;
    QSlider* m_volumeSlider = nullptr;
    TransportControls* m_transport = nullptr;
    SourcesPanel* m_sources = nullptr;
    StatusInfoBar* m_statusInfo = nullptr;
    QLabel* m_viewerZoomStatus = nullptr;
    QDockWidget* m_sourcesDock = nullptr;
    QDockWidget* m_bookmarksDock = nullptr;
    bool m_skipLayoutSaveOnce = false;

    /// Directory the last Open Media dialog was pointed at.
    QString m_lastMediaDirectory;
};

} // namespace atk::ui
