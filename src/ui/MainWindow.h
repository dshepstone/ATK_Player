#pragma once

#include "core/commands/CommandDefinitions.h"
#include "core/commands/CommandId.h"
#include "export/ExportSpec.h"
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
class QSplitter;
class QProgressDialog;
class QJsonObject;

namespace atk::api { class ApiServer; }
namespace atk::api { struct ApiResponse; }
namespace atk::playback { class CompareSession; class CompareVideoLane; enum class CompareLayout; enum class CompareAudioMode; }
namespace atk::project { class Project; }
namespace atk::media { class PlaylistProbeWorker; }
namespace atk::exporter { class ExportJob; }
namespace atk::timeline { class TimelineModel; }

namespace atk::ui {

class CommandRegistry;
class ApplicationSettings;
class BookmarkPanel;
class CompareBar;
class ComparisonCompositeWidget;
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
    bool isComparisonActive() const;
    playback::CompareSession* compareSession() const { return m_compare.get(); }
    playback::CompareVideoLane* compareVideoLane() const { return m_compareLane.get(); }
    ViewerWidget* viewerA() const { return m_viewer; }
    ViewerWidget* viewerB() const { return m_viewerB; }
    ComparisonCompositeWidget* comparisonComposite() const { return m_compareComposite; }
    exporter::ExportSpec exportSnapshot(const QString& outputPath = {}) const;
    bool exportInProgress() const;
    api::ApiServer* apiServer() const { return m_apiServer.get(); }

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
    bool saveProjectTo(const QString& path, bool saveAs = false, bool showError = true);
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
    void enterComparison();
    void exitComparison();
    void setComparisonLayout(playback::CompareLayout layout);
    void setSourceBOffsetFrames(int frames);
    void setExternalAudioOffsetFrames(int frames);
    qint64 offsetUsForFrames(int frames) const;
    int framesForOffsetUs(qint64 offsetUs) const;
    void reanchorComparisonFollowers(bool waveformMappingOnly);
    bool selectComparisonSourceA(const QUuid& id);
    bool selectComparisonSourceB(const QUuid& id);
    void openComparisonSourceB();
    void synchronizeComparison(const media::VideoFrame& sourceAFrame);
    int defaultComparisonBIndex(int sourceAIndex) const;
    bool sourceUsableForComparison(int index) const;
    void refreshComparisonUi();
    void applyComparisonAudioMode(playback::CompareAudioMode mode);
    void loadExternalAudio();
    void clearExternalAudio();
    void exportReview();
    void exportCurrentFrame();
    void exportImageSequence();
    void startExport(exporter::ExportSpec spec, bool showProgressUi = true);
    api::ApiResponse handleApiApplicationCommand(const QString& command,
                                                 const QJsonObject& params);
    bool cancelExportForProjectChange();
    ViewerWidget* activeViewer() const;


    std::unique_ptr<ApplicationSettings> m_settings;

    // Session models. Declared before the widgets that observe them so
    // destruction runs in the safe order.
    std::unique_ptr<timeline::TimelineModel> m_timeline;
    std::unique_ptr<playback::PlaybackController> m_playback;
    std::unique_ptr<project::Project> m_project;
    std::unique_ptr<playback::CompareSession> m_compare;
    std::unique_ptr<playback::CompareVideoLane> m_compareLane;
    std::unique_ptr<api::ApiServer> m_apiServer;
    std::unique_ptr<exporter::ExportJob> m_exportJob;

    CommandRegistry* m_commands = nullptr;
    BookmarkPanel* m_bookmarks = nullptr;

    ViewerWidget* m_viewer = nullptr;
    ViewerWidget* m_viewerB = nullptr;
    CompareBar* m_compareBar = nullptr;
    QWidget* m_compareHost = nullptr;
    QSplitter* m_compareSplitter = nullptr;
    ComparisonCompositeWidget* m_compareComposite = nullptr;
    QVBoxLayout* m_compareLayout = nullptr;
    VideoFullscreenWindow* m_videoFullscreenWindow = nullptr;
    QVBoxLayout* m_centralLayout = nullptr;
    ViewerTransform m_normalViewerTransform;
    ViewerTransform m_normalViewerBTransform;
    QSize m_normalViewerSize;
    QWidget* m_fullscreenPresentation = nullptr;
    ViewerTransform m_normalCompositeTransform;
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
    QProgressDialog* m_exportProgress = nullptr;
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
    QString m_apiExportJobId;
    QString m_apiExportState = QStringLiteral("idle");
    QString m_apiExportOutputPath;
    QString m_apiExportError;
    QString m_lastProjectSaveError;
    int m_apiExportProgress = 0;
    qint64 m_apiExportFrame = 0;
    qint64 m_apiExportTotal = 0;

    /// Directory the last Open Media dialog was pointed at.
    QString m_lastMediaDirectory;
};

} // namespace atk::ui
