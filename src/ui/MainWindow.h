#pragma once

#include "core/commands/CommandDefinitions.h"
#include "core/commands/CommandId.h"
#include "media/MediaMetadata.h"
#include "playback/PlaybackController.h"
#include "ui/ViewerTransform.h"

#include <QMainWindow>
#include <QStringList>
#include <QUuid>

#include <memory>

class QDockWidget;
class QLabel;
class QSpinBox;
class QSlider;
class QCloseEvent;
class QMenu;
class QThread;
class QVBoxLayout;

namespace atk::api { class ApiServer; }
namespace atk::playback { class CompareSession; }
namespace atk::project { class Project; }
namespace atk::media { class PlaylistProbeWorker; }
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
class VideoFullscreenWindow;

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
    project::Project* project() const { return m_project.get(); }
    bool openProjectFile(const QString& path);
    void reopenLastProjectIfEnabled();
    bool isVideoFullScreen() const;
    void enterVideoFullScreen();
    void exitVideoFullScreen();

protected:
    void closeEvent(QCloseEvent* event) override;

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
    void addMediaDialog();
    void addMediaFiles(const QStringList& paths);
    void newProject();
    void openProjectDialog();
    bool saveProject();
    bool saveProjectAs();
    bool saveProjectTo(const QString& path);
    bool confirmDiscardChanges();
    void activatePlaylistIndex(int index, bool continuePlayback = false);
    int nextUsablePlaylistIndex() const;
    void saveActiveReviewState();
    void restoreActiveReviewState();
    void removePlaylistIndex(int index);
    void movePlaylistIndex(int from, int to);
    void relinkSelectedMedia();
    void refreshRecentProjectsMenu();
    void startPlaylistProbes();
    void startProbe(const QUuid& id, const QString& path);


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
    VideoFullscreenWindow* m_videoFullscreenWindow = nullptr;
    QVBoxLayout* m_centralLayout = nullptr;
    ViewerTransform m_normalViewerTransform;
    QSize m_normalViewerSize;
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
    QMenu* m_recentProjectsMenu = nullptr;
    bool m_skipLayoutSaveOnce = false;
    bool m_restoringSourceState = false;
    bool m_playAfterSourceOpen = false;
    bool m_playlistPlaybackActive = false;
    QThread* m_probeThread = nullptr;
    media::PlaylistProbeWorker* m_probeWorker = nullptr;
    quint64 m_nextProbeToken = 1;
    QUuid m_pendingRelinkId;
    QString m_pendingRelinkPath;
    quint64 m_pendingRelinkToken = 0;
    quint64 m_pendingRelinkProjectGeneration = 0;
    quint64 m_projectGeneration = 1;
    bool m_suppressProjectOpenError = false;

    /// Directory the last Open Media dialog was pointed at.
    QString m_lastMediaDirectory;
};

} // namespace atk::ui
