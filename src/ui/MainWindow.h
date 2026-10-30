#pragma once

#include "annotation/AnnotationGhosting.h"
#include "annotation/AnnotationHold.h"
#include "annotation/AnnotationToolSettings.h"
#include "core/commands/CommandDefinitions.h"
#include "core/commands/CommandId.h"
#include "export/ExportSpec.h"
#include "media/MediaMetadata.h"
#include "media/ImageSource.h"
#include "playback/PlaybackController.h"
#include "timeline/PlaybackRange.h"
#include "ui/ArrangePlaylistDialog.h"
#include "ui/ViewerTransform.h"

#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QUuid>

#include <QHash>
#include <QUuid>

#include <memory>
#include <optional>

class QDockWidget;
class QLabel;
class QSpinBox;
class QToolButton;
class QSlider;
class QCloseEvent;
class QMenu;
class QThread;
class QToolBar;
class QVBoxLayout;
class QSplitter;
class QProgressDialog;
class QJsonObject;
class QStackedWidget;

namespace atk::annotation { class AnnotationStore; class BrushLibrary; }
namespace atk::api { class ApiServer; }
namespace atk::api { struct ApiResponse; }
namespace atk::playback { class CompareSession; class CompareVideoLane; enum class CompareLayout; enum class CompareAudioMode; }
namespace atk::project { class Project; struct SourceEntry; }
namespace atk::media { class PlaylistProbeWorker; class ThumbnailWorker; }
namespace atk::exporter { class ExportJob; class ReviewNotesJob; struct ReviewNotesOptions; struct ReviewNotesDocument; }
namespace atk::timeline { class TimelineModel; }
namespace atk::review { class ReviewLibrary; struct FoundClip; struct RenameDecision; }

namespace atk::ui {

class AnnotationToolbar;
struct NotesPdfBatch;
class NotesPanel;
class ExportProgressDialog;
class CommandRegistry;
class ApplicationSettings;
class BookmarkPanel;
class BrushSizePill;
class ReviewHeaderBar;
class ReviewLibraryPage;
class CompareBar;
class ComparisonCompositeWidget;
class FrameNumberInput;
class MediaInformationDialog;
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
    /// Reopens the last project when that preference is on; true if it did.
    bool reopenLastProjectIfEnabled();

    // --- Review Library (the start screen) -------------------------------
    /// What a launch without a file shows: the Review Library, unless the
    /// user turned that off in Preferences (then the empty player).
    void showStartScreen();
    void showReviewLibrary();
    /// Back to the player. Every way of opening a file goes through here, so
    /// a clip never loads behind the library.
    void showPlayer();
    bool isShowingReviewLibrary() const;
    review::ReviewLibrary* reviewLibrary() const { return m_reviewLibrary.get(); }
    ReviewLibraryPage* reviewLibraryPage() const { return m_libraryPage; }
    /// Makes a review: a project of `clips` in this order, saved as
    /// `projectPath`, added to the library and opened. False (with a message
    /// unless `interactive` is off) when it cannot be saved.
    bool createReview(const QString& name, const QString& folder, bool includeSubfolders,
                      const QVector<review::FoundClip>& clips, const QString& projectPath, bool interactive = true);
    /// Opens a library review; offers to add clips that appeared in its folder.
    bool openReview(const QUuid& id, bool interactive = true);
    /// Lists existing .atkproj files in the library without opening them
    /// (Add Existing). Returns how many were listed; unreadable files are
    /// named in `failed`. One already listed is brought up to date instead.
    int addExistingReviews(const QStringList& paths, QStringList* failed = nullptr);
    /// Applies a Rename from Team List decision (what the dialog returns):
    /// shown names, order, and, with `onDisk`, the checked file renames.
    /// False (and nothing changed on disk) when the renames failed.
    bool applyRosterRename(const QVector<review::RenameDecision>& decisions, bool onDisk, bool reorder,
                           QString* error = nullptr);
    /// Puts back the names from a finished rename journal.
    bool undoRosterRename(const QString& journalPath, QString* error = nullptr);

    /// What happens when a clip of the playlist finishes playing. The loop
    /// button (L) switches between the two loop modes and the two others,
    /// each side keeping the choice last made for it.
    enum class PlaylistMode {
        PlayThrough,   ///< on to the next clip; stop after the last (default)
        StopAfterClip, ///< stop at the end of each clip
        LoopClip,      ///< play the current clip again
        LoopPlaylist,  ///< on to the next clip; the last goes on to the first
    };
    PlaylistMode playlistMode() const { return m_playlistMode; }
    void setPlaylistMode(PlaylistMode mode);
    /// Shows the welcome dialog once per installed version. Called by main()
    /// after the window is up; tests construct the window without it.
    void showWelcomeIfFirstRun();
    void showWelcome();
    bool isVideoFullScreen() const;
    /// Mirrors every viewer (single, A/B panes and composite) at paint time.
    /// Session-only presentation state: reset when a source opens, never
    /// saved, never applied to exports.
    void setFlipHorizontal(bool flipped);
    bool isFlippedHorizontally() const { return m_flipHorizontal; }
    /// Re-times an image sequence or still: frame indices (and therefore
    /// bookmarks and the review range) are unchanged, only the rate. Reopens
    /// the source if it is playing or is comparison Source B.
    bool setSourceFrameRate(int playlistIndex, const media::FrameRate& rate);
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
    /// True when the project has a save location for exports and it exists.
    bool projectExportFolderAvailable() const;
    /// Applies an Arrange Playlist result: the categories, each clip's
    /// category and the new order.
    void applyPlaylistArrangement(const QVector<ArrangePlaylistDialog::Clip>& clips,
                                  const QStringList& categories);
    /// The In/Out toggle beside the review range (for tests).
    QToolButton* reviewRangeToggle() const { return m_reviewRangeToggle; }
    /// Keeps (or forgets) the burn-in choices the review's exports start from.
    void rememberExportBurnIns(const std::optional<exporter::ExportBurnIns>& burnIns);
    /// The last export handed to the system viewer (for tests).
    QString lastOpenedExport() const { return m_lastOpenedExport; }
    /// Opens a finished PDF or video in the system viewer once the file is
    /// free (a sync app such as Google Drive may hold it for a moment).
    void openExportedFile(const QString& path);
    /// Export Notes PDFs for Playlist without the dialog: one PDF per clip in
    /// `clips` (playlist ids), named after the clip, into `folder`, one after
    /// another. An existing PDF is replaced, or with `replaceExisting` off the
    /// new one is saved as "name (2).pdf". False when nothing could start.
    bool exportNotesPdfs(const QVector<QUuid>& clips, const QString& folder,
                         const exporter::ReviewNotesOptions& options, bool replaceExisting,
                         bool openFolderWhenDone = false);
    bool notesPdfBatchInProgress() const { return m_notesBatch != nullptr; }
    /// PDFs the last batch saved, and the clips it could not export (tests).
    QStringList lastNotesPdfBatchSaved() const { return m_lastNotesBatchSaved; }
    QStringList lastNotesPdfBatchFailed() const { return m_lastNotesBatchFailed; }
    api::ApiServer* apiServer() const { return m_apiServer.get(); }

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// Dropping supported local media files onto the window adds them to the
    /// playlist, exactly like Add to Playlist; nothing is replaced or prompted.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

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
    void openMediaInformation();
    void refreshMediaInformation();
    void applyPreferences(const PreferencesDialog& dialog);
    void restoreApplicationLayout();
    void saveApplicationLayout();
    void addMediaDialog();
    /// Adds media to the playlist. With `interactive`, a numbered image whose
    /// neighbours exist asks whether to open the whole sequence; the API
    /// passes false and always gets single files, so automation never blocks.
    void addMediaFiles(const QStringList& paths, bool interactive = true);
    enum class ImageInterpretation { Sequence, SingleStill, Skip };
    ImageInterpretation askImageInterpretation(const media::ImageSequence& sequence,
                                               const QString& path);
    void changeSourceFrameRate(int playlistIndex);
    void newProject();
    void openProjectDialog();
    void addExistingReviewDialog();
    bool saveProject();
    bool saveProjectAs();
    bool saveProjectTo(const QString& path, bool saveAs = false, bool showError = true);
    bool confirmDiscardChanges();
    void activatePlaylistIndex(int index, bool continuePlayback = false);
    /// The next playable clip after the current one, or -1. With `wrap`
    /// the search goes on from the first clip (up to the current one).
    int nextUsablePlaylistIndex(bool wrap = false) const;
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
    /// Export Review; `withNotes` turns on the drawings and note captions
    /// (Export Notes Video).
    void exportReview(bool withNotes = false);
    void exportCurrentFrame();
    void exportImageSequence();
    /// Starts a still export from the review's saved burn-in choices, if any.
    void applySavedStillBurnIns(exporter::ExportSpec& spec) const;
    /// The Arrange Playlist window: bulk reorder, sort and categories.
    void arrangePlaylist();
    /// Plays only the in/out range (back to the last one), or the whole clip
    /// keeping the range. The In/Out button and Shift+I.
    void setReviewRangeActive(bool active);
    void syncReviewRangeToggle();
    /// The review range a clip saves: the one on screen, or the one the
    /// in/out toggle would switch back to, marked off.
    timeline::PlaybackRange reviewRangeToSave() const;
    void exportReviewNotes();
    void exportReviewNotesBatch();
    exporter::ExportSpec notesPdfSpec(const project::SourceEntry& entry) const;
    exporter::ReviewNotesDocument notesPdfHeader(const exporter::ExportSpec& spec, bool includeResolved) const;
    /// Every clip's spec, header and file name, decided before anything is
    /// written. With `replaceExisting` off, names already in `folder` are
    /// numbered on; repeated clips are numbered either way.
    std::unique_ptr<NotesPdfBatch> planNotesPdfs(const QVector<QUuid>& clips, const QString& folder,
                                                 const exporter::ReviewNotesOptions& options,
                                                 bool replaceExisting) const;
    void startNextNotesPdf();
    void finishNotesPdfBatch(bool cancelled);
    bool notesPdfInProgress() const;
    void startExport(exporter::ExportSpec spec, bool showProgressUi = true);
    api::ApiResponse handleApiApplicationCommand(const QString& command,
                                                 const QJsonObject& params);
    bool cancelExportForProjectChange();
    ViewerWidget* activeViewer() const;

    // Drawing on frames. The store holds the active source's annotations; the
    // viewer only displays the displayed frame's and reports new strokes.
    void setAnnotateMode(bool enabled);
    void bindAnnotationsToActiveSource();
    void refreshViewerAnnotations();
    void applyAnnotationTool(const annotation::AnnotationToolSettings& settings);
    /// View aids: tracing paper (percent, 0 = off) and ghosting. Each updates
    /// the viewer, the toolbar, the menu check and the saved settings.
    void applyTracingPaper(int percent);
    void applyGhostSettings(const annotation::GhostSettings& settings);
    /// Hold drawings until the next drawing (Off, Add to Current, Add New).
    /// Updates the viewer, the timeline, the menu checks and the settings.
    void applyHoldMode(annotation::HoldMode mode, bool announce = true);
    void refreshHoldSpans();
    /// The frame an edit on the frame on screen belongs to. While a held
    /// drawing is on screen, edits to it go to the frame it was drawn on, and
    /// so does a new drawing in Add to Current mode; in Add New mode a new
    /// drawing goes on the frame on screen.
    qint64 annotationEditFrame(qint64 frameIndex, bool newDrawing) const;
    /// The speed button's label follows the speed and the media's rate.
    void refreshPlaybackSpeed();
    void zoomViewerToPercent(qreal percent);
    void seekAnnotatedFrame(int direction);
    void importBrushSet();
    void refreshNotesPanel();
    void removeBrushSet(const QString& name);
    void updateAnnotationActions();
    /// Copy / Cut: the drawing picked with the Select tool, or else every
    /// drawing and note on the frame on screen. Paste adds the clipboard to
    /// the frame on screen as one undo step.
    void copyAnnotations(bool cut);
    void pasteAnnotations();
    void copyNote(qint64 frame, quint64 noteId, bool cut);
    /// A drawing marker dragged along the timeline from one frame to another.
    void moveAnnotatedFrame(qint64 from, qint64 to, bool copy);
    void moveAnnotatedFrames(const QVector<qint64>& frames, qint64 offset, bool copy);


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
    std::unique_ptr<exporter::ReviewNotesJob> m_notesPdfJob;
    /// Export Notes PDFs for Playlist: the clips still to do, one job at a time.
    std::unique_ptr<NotesPdfBatch> m_notesBatch;
    QStringList m_lastNotesBatchSaved;
    QStringList m_lastNotesBatchFailed;
    std::unique_ptr<annotation::AnnotationStore> m_annotations;
    /// Imported brush sets. Kept beside the settings file when one is given
    /// (tests), otherwise in the per-user application data folder.
    std::unique_ptr<annotation::BrushLibrary> m_brushLibrary;
    QString m_settingsIniPath;
    /// Source the annotation store is bound to; edits are written back to it.
    QUuid m_annotationSourceId;
    /// The frame on screen in viewer A, and its picture height in pixels.
    qint64 m_displayedFrameIndex = -1;
    int m_displayedImageHeight = 0;
    /// Decoded presentation time of that frame; stored with new drawings.
    qint64 m_displayedFramePtsUs = 0;
    qint64 annotationMediaTimeUs(qint64 frameIndex) const;
    /// Drawings and notes copied or cut, ready to paste on any frame or clip.
    /// Positions are normalised to the picture, so they land in the same place.
    annotation::FrameAnnotation m_annotationClipboard;

    CommandRegistry* m_commands = nullptr;
    BookmarkPanel* m_bookmarks = nullptr;
    AnnotationToolbar* m_annotationToolbar = nullptr;
    /// Size and opacity, floating at the viewer's left edge while drawing.
    BrushSizePill* m_brushSizePill = nullptr;
    void placeBrushSizePill();
    /// Project › clip and the clip's status, above the picture.
    ReviewHeaderBar* m_reviewHeader = nullptr;
    QToolBar* m_reviewHeaderToolbar = nullptr;
    void refreshReviewHeader();
    std::unique_ptr<review::ReviewLibrary> m_reviewLibrary;
    QStackedWidget* m_pages = nullptr;
    QWidget* m_playerPage = nullptr;
    ReviewLibraryPage* m_libraryPage = nullptr; // built the first time it is shown
    /// Docks shown before the library hid them, restored by showPlayer().
    QList<QWidget*> m_hiddenForLibrary;
    QHash<QUuid, QString> m_previewThumbnailRequests;
    void newReviewDialog();
    void renameClipsFromRoster();
    /// Points the project's clips at their files' new names after a disk
    /// rename (or its undo): `moved` maps old path to new path.
    void retargetRenamedClips(const QHash<QString, QString>& moved);
    /// Offers to restore names from a rename in `folder` that never finished.
    void offerRenameRecovery(const QString& folder);
    bool saveReviewLibrary();
    /// Keeps the library's clip count and preview strip in step with the
    /// open project when it belongs to a review.
    void syncReviewLibraryEntry();
    void addOpenedProjectToReviewLibrary();
    void requestPreviewThumbnail(const QString& clipPath);
    QString thumbnailCacheDirectory() const;
    // Playlist thumbnails: a worker on its own thread, like the probe worker.
    QThread* m_thumbnailThread = nullptr;
    media::ThumbnailWorker* m_thumbnailWorker = nullptr;
    quint64 m_nextThumbnailToken = 1;
    void requestThumbnail(const QUuid& sourceId);
    int m_tracingPaper = 0;
    int m_tracingPaperLevel = 50;
    annotation::GhostSettings m_ghostSettings;
    annotation::HoldMode m_holdMode = annotation::HoldMode::Off;
    /// The mode Hold Drawings turns on with: the last one chosen.
    annotation::HoldMode m_holdOnMode = annotation::HoldMode::AddToCurrent;
    /// While the frame on screen shows a held drawing, the frame it was drawn
    /// on; -1 otherwise.
    qint64 m_heldSourceFrame = -1;
    NotesPanel* m_notesPanel = nullptr;
    QDockWidget* m_notesDock = nullptr;

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
    FrameNumberInput* m_frameNumberInput = nullptr;
    QPointer<MediaInformationDialog> m_mediaInformationDialog;
    TimelineRangeSlider* m_timelineRangeSlider = nullptr;
    QToolButton* m_reviewRangeToggle = nullptr;
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
    ExportProgressDialog* m_exportProgress = nullptr;
    ExportProgressDialog* m_notesPdfProgress = nullptr;
    bool m_openExportWhenDone = false; // set per Export Review run
    QString m_lastOpenedExport;
    bool m_skipLayoutSaveOnce = false;
    bool m_restoringSourceState = false;
    bool m_playAfterSourceOpen = false;
    bool m_playlistPlaybackActive = false;
    PlaylistMode m_playlistMode = PlaylistMode::PlayThrough;
    bool m_stopAfterEachClip = false; // the choice for when loop is off
    bool m_loopWholePlaylist = false; // the choice for when loop is on
    QThread* m_probeThread = nullptr;
    media::PlaylistProbeWorker* m_probeWorker = nullptr;
    quint64 m_nextProbeToken = 1;
    QUuid m_pendingRelinkId;
    QString m_pendingRelinkPath;
    bool m_flipHorizontal = false;
    /// Hold the pending relink was validated with; the replacement gets it.
    media::ImageSourceOptions m_pendingRelinkStill;
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
