#include "ui/MainWindow.h"

#include "annotation/AbrReader.h"
#include "annotation/AnnotationGhosting.h"
#include "annotation/AnnotationStore.h"
#include "annotation/BrushLibrary.h"
#include "api/ApiServer.h"
#include "platform/PlatformInfo.h"
#include "playback/CompareSession.h"
#include "playback/CompareVideoLane.h"
#include "core/Logging.h"
#include "core/Version.h"
#include "media/MediaSource.h"
#include "media/CompareAudioWorker.h"
#include "media/PlaylistProbeWorker.h"
#include "media/ThumbnailWorker.h"
#include "export/ExportJob.h"
#include "export/FFmpegExporter.h"
#include "export/ReviewNotesPdf.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "timeline/Timecode.h"
#include "timeline/TimelineModel.h"
#include "ui/AnnotationToolbar.h"
#include "ui/ApplicationSettings.h"
#include "ui/BookmarkPanel.h"
#include "ui/BrushSizePill.h"
#include "ui/ArrangePlaylistDialog.h"
#include "ui/BurnInOptionsWidget.h"
#include "ui/CompareBar.h"
#include "ui/ComparisonCompositeWidget.h"
#include "ui/ExportDialog.h"
#include "ui/ExportUi.h"
#include "ui/FrameFieldStyle.h"
#include "ui/FrameNumberInput.h"
#include "ui/IconProvider.h"
#include "ui/MediaInformationDialog.h"
#include "ui/NotesPanel.h"
#include "ui/PreferencesDialog.h"
#include "ui/Resources.h"
#include "review/BatchRename.h"
#include "review/FolderScanner.h"
#include "review/ReviewLibrary.h"
#include "ui/BatchRenameDialog.h"
#include "ui/NewReviewDialog.h"
#include "ui/ReviewHeaderBar.h"
#include "ui/ReviewLibraryPage.h"
#include "ui/ReviewNotesDialog.h"
#include "ui/BatchNotesPdfDialog.h"
#include "ui/SourcesPanel.h"
#include "ui/StatusInfoBar.h"
#include "ui/Theme.h"
#include "ui/TimelineWidget.h"
#include "ui/TimelineRangeSlider.h"
#include "ui/TransportControls.h"
#include "ui/ViewerWidget.h"
#include "ui/WelcomeDialog.h"
#include "ui/VideoFullscreenWindow.h"
#include "ui/commands/CommandRegistry.h"

#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QWidgetAction>
#include <QThread>
#include <QTimer>
#include <QWindow>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHash>
#include <QInputDialog>
#include <QPushButton>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDialogButtonBox>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QDateTime>
#include <QDesktopServices>
#include <QLocale>
#include <QFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QProgressDialog>
#include <QShortcut>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace atk::ui {

/// Export Notes PDFs for Playlist: everything decided up front (clips, header
/// text, file names), then one ReviewNotesJob after another.
struct NotesPdfBatch {
    struct Item {
        QString name;
        exporter::ExportSpec spec;
        exporter::ReviewNotesDocument header;
        QString outputPath;
    };
    QVector<Item> items;
    int next = 0;
    exporter::ReviewNotesOptions options;
    QString folder;
    bool openFolderWhenDone = false;
    bool cancelRequested = false;
    QStringList saved;
    QStringList failed;
};

namespace {
using commands::CommandId;

bool chooseBurnIns(QWidget* parent, const QString& title, const QString& summary, exporter::ExportBurnIns& result)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setStyleSheet(exportui::styleSheet());
    dialog.setMinimumWidth(520);
    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    exportui::addHeader(root, &dialog, QStringLiteral("PNG"), title, summary);
    auto* body = exportui::addBody(root);
    auto* card = exportui::addCard(body, &dialog, QObject::tr("Drawings, notes and burn-ins"));
    auto* options = new BurnInOptionsWidget(&dialog); options->setOptions(result);
    options->setVideoOptionsVisible(false); // stills: no play-through, hold or freeze
    card->addWidget(options);
    exportui::addFooter(root, &dialog, QObject::tr("Export"));
    if (dialog.exec() != QDialog::Accepted) return false;
    result = options->options();
    return true;
}

/// Commands that get a separator drawn above them, to group the menus without
/// hard-coding the menu structure a second time.
bool startsNewGroup(CommandId id)
{
    switch (id) {
    case CommandId::ExportReviewNotes: // the notes exports, PDF and video
    case CommandId::CloseSource:
    case CommandId::Quit:
    case CommandId::ToggleAnnotateMode:
    case CommandId::IncreaseBrushSize:
    case CommandId::PreviousFrame:
    case CommandId::ToggleLoop:
    case CommandId::PlaylistPlayThrough:
    case CommandId::SetRangeIn:
    case CommandId::AddBookmark:
    case CommandId::VolumeUp:
    case CommandId::ZoomFit:
    case CommandId::ToggleFullScreen:
        return true;
    default:
        return false;
    }
}

/// Phase 0 placeholder extent.
///
/// There is no decoder yet, so without an extent the transport would be inert
/// and unverifiable -- stepping, seeking and looping would all clamp to frame
/// zero. Installing a nominal 100 frames at 24 fps makes the whole transport
/// exercisable now, and every readout that displays these numbers marks itself
/// as placeholder so the application never looks as though a file is open.
///
/// Opening real media in M1 replaces this via PlaybackController::setSource().
constexpr int64_t kPlaceholderFrameCount = 100;
constexpr int kPlaceholderFps = 24;

/// Video extensions offered by the media dialogs and accepted by drag and drop
/// (shared with the review folder scan). "All Files" stays available.
const QStringList& videoExtensions()
{
    return media::videoFileExtensions();
}

QString globList(const QStringList& extensions)
{
    QStringList globs;
    for (const QString& extension : extensions) globs << QStringLiteral("*.") + extension;
    return globs.join(QLatin1Char(' '));
}

/// One filter for Open Media, Add to Playlist and Relink, so the three cannot
/// drift apart. Still-image extensions come from media::stillImageExtensions(),
/// the same list the decoder classifies by.
QString mediaFileFilter()
{
    const QString video = globList(videoExtensions());
    const QString images = globList(media::stillImageExtensions());
    return MainWindow::tr("Media Files (%1 %2);;Video Files (%1);;Image Files (%2);;All Files (*.*)")
        .arg(video, images);
}

using media::isSupportedMediaPath;

/// Local, existing, supported media files carried by a drag.
QStringList droppedMediaPaths(const QMimeData* mime)
{
    QStringList paths;
    if (mime == nullptr || !mime->hasUrls()) return paths;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) continue;
        const QFileInfo info(url.toLocalFile());
        if (info.isFile() && isSupportedMediaPath(info.filePath()))
            paths << info.absoluteFilePath();
    }
    return paths;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(QString(), parent)
{
}

QString safeImagePrefix(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])")), QStringLiteral("_"));
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' '))) value.chop(1);
    return value.isEmpty() ? QStringLiteral("frames") : value;
}

/// Name exports after the source; a sequence pattern loses its frame token
/// ("shot.%04d.exr" -> "shot") rather than leaking it into file names.
QString exportBaseName(const exporter::ExportSource& source)
{
    return source.still.isSequence() ? media::sequenceBaseName(source.path)
                                     : QFileInfo(source.path).completeBaseName();
}

/// Whether a saved main-window state puts the review header on the top row by
/// itself, with the drawing tools on the row below. Read from a stand-in
/// window with toolbars of the same names, so the real window never has to
/// restore a state and then rearrange it.
bool savedStateKeepsHeaderOnTop(const QByteArray& state, const QString& headerName, const QString& drawingName)
{
    QMainWindow standIn;
    auto* header = new QToolBar(&standIn);
    header->setObjectName(headerName);
    auto* drawing = new QToolBar(&standIn);
    drawing->setObjectName(drawingName);
    standIn.addToolBar(Qt::TopToolBarArea, header);
    standIn.addToolBar(Qt::TopToolBarArea, drawing);
    if (!standIn.restoreState(state)) return false;
    // With only these two toolbars, this is exactly "header row, then the
    // drawing tools' row".
    return standIn.toolBarArea(header) == Qt::TopToolBarArea && standIn.toolBarArea(drawing) == Qt::TopToolBarArea
        && !standIn.toolBarBreak(header) && standIn.toolBarBreak(drawing);
}

MainWindow::MainWindow(const QString& settingsIniPath, QWidget* parent)
    : QMainWindow(parent)
{
    ensureResourcesInitialized();
    if (!settingsIniPath.isEmpty())
        m_settings = std::make_unique<ApplicationSettings>(settingsIniPath);
    m_settingsIniPath = settingsIniPath;
    setObjectName(QStringLiteral("AtkMainWindow"));
    setWindowIcon(icons::applicationIcon());
    setAcceptDrops(true);

    buildModels();
    buildWidgets();
    buildMenus();
    // Next/previous note or drawing are Ctrl + the < and > keys. Their
    // defaults are the unshifted keys (Ctrl+, / Ctrl+.); these also accept the
    // shifted form, for whoever presses Ctrl+Shift+< / >.
    for (const auto& [sequence, id] : {std::pair{"Ctrl+<", CommandId::PreviousAnnotatedFrame},
                                       std::pair{"Ctrl+>", CommandId::NextAnnotatedFrame}}) {
        auto* shortcut = new QShortcut(QKeySequence(QString::fromLatin1(sequence)), this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, id = id] {
            if (QAction* action = m_commands->action(id); action && action->isEnabled()) action->trigger();
        });
    }
    buildAudioControls();
    connectSignals();

    if (m_settings->apiEnabled()) m_apiServer->start(static_cast<quint16>(m_settings->apiPort()));

    updateWindowTitle();
    resize(1280, 800);
    restoreApplicationLayout();
    // Bookmarks open on demand (F5, View menu, or Add Range Bookmark); the
    // viewer gets the full width by default whatever layout was last saved.
    m_bookmarksDock->hide();

    qCInfo(log::ui) << "Main window constructed";
}

MainWindow::~MainWindow()
{
    if (m_exportJob) { m_exportJob->cancel(); m_exportJob->wait(); }
    exitVideoFullScreen();
    saveApplicationLayout();
    if (m_probeThread) {
        m_probeThread->quit();
        m_probeThread->wait();
    }
    // Stop delivery, then let the worker finish its current clip and go.
    if (m_thumbnailWorker) m_thumbnailWorker->disconnect(this);
    if (m_thumbnailThread) {
        m_thumbnailThread->quit();
        m_thumbnailThread->wait();
    }
}

void MainWindow::buildModels()
{
    if (!m_settings) m_settings = std::make_unique<ApplicationSettings>();
    m_timeline = std::make_unique<timeline::TimelineModel>();
    m_playback = std::make_unique<playback::PlaybackController>(m_timeline.get());
    m_playback->setAudioScrubEnabled(m_settings->audioScrubEnabled());
    m_playback->setFrameStepAudioEnabled(m_settings->frameStepAudioEnabled());
    m_playback->setVolume(m_settings->volume());
    m_playback->setMuted(m_settings->muted());
    m_project  = std::make_unique<project::Project>();
    m_annotations = std::make_unique<annotation::AnnotationStore>();
    const QString brushDirectory = !m_settingsIniPath.isEmpty()
        ? QFileInfo(m_settingsIniPath).absoluteDir().filePath(QStringLiteral("brushes"))
        : QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("brushes"));
    m_brushLibrary = std::make_unique<annotation::BrushLibrary>(brushDirectory);
    m_brushLibrary->load();
    m_brushLibrary->registerAll();
    // The review index: beside the settings file when one was given (tests),
    // else in the user's application data.
    m_reviewLibrary = std::make_unique<review::ReviewLibrary>(
        !m_settingsIniPath.isEmpty()
            ? QFileInfo(m_settingsIniPath).absoluteDir().filePath(QStringLiteral("reviews.json"))
            : QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("reviews.json")));
    if (QString error; !m_reviewLibrary->load(&error))
        qCWarning(log::ui) << "Review library not read:" << error;
    m_probeThread = new QThread(this);
    m_probeWorker = new media::PlaylistProbeWorker;
    m_probeWorker->moveToThread(m_probeThread);
    connect(m_probeThread, &QThread::finished, m_probeWorker, &QObject::deleteLater);
    connect(m_probeWorker, &media::PlaylistProbeWorker::probeFinished, this,
        [this](QUuid id, const QString& path, quint64 token, const media::MediaMetadata& metadata,
               const QString& error, bool missing) {
            if (token == m_pendingRelinkToken && id == m_pendingRelinkId && path == m_pendingRelinkPath) {
                m_pendingRelinkToken = 0;
                if (m_pendingRelinkProjectGeneration != m_projectGeneration) return;
                if (missing || !error.isEmpty() || !metadata.isValid()) {
                    QMessageBox::critical(this, tr("Relink Media"),
                        missing ? tr("The selected media file is missing.")
                                : tr("ATK Player could not open the replacement media: %1").arg(error));
                    return;
                }
                const int index = m_project->indexForId(id);
                if (index < 0) return;
                auto replacement = std::make_shared<media::MediaSource>(path);
                replacement->setMetadata(metadata);
                replacement->setImageOptions(m_pendingRelinkStill);
                const int64_t frameCount = metadata.effectiveFrameCount();
                if (frameCount <= 0 || !m_project->relinkSource(id, replacement, frameCount)) return;
                if (index == m_project->activeIndex()) {
                    m_project->setActiveIndex(-1);
                    activatePlaylistIndex(index, false);
                } else if (isComparisonActive() && id == m_compare->sourceBId()) {
                    openComparisonSourceB();
                    if (m_compare->audioMode() == playback::CompareAudioMode::SourceB)
                        applyComparisonAudioMode(playback::CompareAudioMode::SourceB);
                }
                return;
            }
            m_project->applyProbeResult(id, path, token, metadata, error, missing);
        });
    m_probeThread->start();
    m_thumbnailThread = new QThread(this);
    m_thumbnailThread->setObjectName(QStringLiteral("ThumbnailThread"));
    m_thumbnailWorker = new media::ThumbnailWorker;
    m_thumbnailWorker->moveToThread(m_thumbnailThread);
    connect(m_thumbnailThread, &QThread::finished, m_thumbnailWorker, &QObject::deleteLater);
    m_thumbnailThread->start(QThread::LowPriority);
    m_compare  = std::make_unique<playback::CompareSession>();
    m_apiServer = std::make_unique<api::ApiServer>(
        m_playback.get(), m_timeline.get(),
        [this](const QString& command, const QJsonObject& params) {
            return handleApiApplicationCommand(command, params);
        });

    // The registry is parented to the window, so its QActions live exactly as
    // long as the widgets that reference them.
    m_commands = new CommandRegistry(this);
    m_commands->applyShortcutOverrides(m_settings->shortcutOverrides());

    // Give the transport something to move against; see the note above.
    installPlaceholderTimeline();

    qCInfo(log::app) << "Session models created";
}

void MainWindow::buildWidgets()
{
    // --- Central column: viewer, timeline, transport ----------------------
    auto* central = new QWidget(this);
    auto* column = new QVBoxLayout(central);
    m_centralLayout = column;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_viewer = new ViewerWidget(central);
    m_viewer->setPlaceholderText(tr("ATK PLAYER"));
    m_viewer->setPlaceholderSubtext(
        tr("No media loaded — video playback arrives in milestone M1"));
    column->addWidget(m_viewer, 1);

    m_compareHost = new QWidget(central);
    auto* compareLayout = new QVBoxLayout(m_compareHost);
    m_compareLayout = compareLayout;
    compareLayout->setContentsMargins(0, 0, 0, 0);
    compareLayout->setSpacing(0);
    m_compareBar = new CompareBar(m_commands, m_compareHost);
    m_compareBar->setProject(m_project.get());
    compareLayout->addWidget(m_compareBar);
    m_compareSplitter = new QSplitter(Qt::Horizontal, m_compareHost);
    m_compareSplitter->setChildrenCollapsible(false);
    m_viewerB = new ViewerWidget(m_compareSplitter);
    m_viewerB->setObjectName(QStringLiteral("CompareViewerB"));
    m_viewerB->setPlaceholderText(tr("B"));
    m_viewerB->setPlaceholderSubtext(tr("Loading comparison source..."));
    m_compareSplitter->addWidget(m_viewerB);
    compareLayout->addWidget(m_compareSplitter, 1);
    m_compareComposite = new ComparisonCompositeWidget(m_compareHost);
    m_compareComposite->hide();
    compareLayout->addWidget(m_compareComposite, 1);
    m_compareHost->hide();

    m_timelineWidget = new TimelineWidget(central);
    m_timelineWidget->setObjectName(QStringLiteral("TimelineWidget"));
    m_timelineWidget->setModel(m_timeline.get());
    m_frameNumberInput = new FrameNumberInput(central);
    m_frameNumberInput->setFrameCount(m_timeline->frameCount());
    m_frameNumberInput->setCurrentFrame(m_timeline->currentFrame());
    auto* timelineRow = new QHBoxLayout;
    timelineRow->setContentsMargins(0, 0, 0, 0);
    timelineRow->setSpacing(0);
    timelineRow->addWidget(m_frameNumberInput);
    timelineRow->addWidget(m_timelineWidget, 1);
    column->addLayout(timelineRow);
    m_timelineRangeSlider = new TimelineRangeSlider(central);
    m_timelineRangeSlider->setObjectName(QStringLiteral("TimelineReviewRangeSlider"));
    m_timelineRangeSlider->setModel(m_timeline.get());
    m_reviewStartFrame = new QSpinBox(central);
    m_reviewStartFrame->setObjectName(QStringLiteral("ReviewRangeStartFrame"));
    m_reviewStartFrame->setKeyboardTracking(false);
    m_reviewStartFrame->setToolTip(tr("First frame in the visible review range. Playback is constrained to this range."));
    m_reviewEndFrame = new QSpinBox(central);
    m_reviewEndFrame->setObjectName(QStringLiteral("ReviewRangeEndFrame"));
    m_reviewEndFrame->setKeyboardTracking(false);
    m_reviewEndFrame->setToolTip(tr("Last frame in the visible review range. Playback is constrained to this range."));
    for (QSpinBox* field : {m_reviewStartFrame, m_reviewEndFrame}) {
        configureFrameField(field);
        updateFrameFieldWidth(field, m_timeline->frameCount());
    }
    // Set In [ and Set Out ]: each marks the current frame on the timeline,
    // and playback keeps between the marks. Right-click either to clear them.
    auto makeMarkButton = [central](const QString& name, const QString& text, const QString& accessible) {
        auto* button = new QToolButton(central);
        button->setObjectName(name);
        button->setProperty("atkRole", QStringLiteral("markButton"));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        button->setAccessibleName(accessible);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        return button;
    };
    m_setInButton = makeMarkButton(QStringLiteral("TimelineSetInButton"), tr("[ In"), tr("Set in point"));
    m_setOutButton = makeMarkButton(QStringLiteral("TimelineSetOutButton"), tr("Out ]"), tr("Set out point"));
    // On/off for the marks: off hides them and plays the whole clip, keeping
    // them to switch back on.
    m_inOutToggle = makeMarkButton(QStringLiteral("TimelineInOutToggle"), tr("Off"), tr("In and out points on"));
    m_inOutToggle->setCheckable(true);
    m_inOutToggle->setMinimumWidth(42);
    auto* reviewRangeRow = new QHBoxLayout;
    reviewRangeRow->setContentsMargins(8, 0, 8, 0);
    reviewRangeRow->setSpacing(6);
    reviewRangeRow->addWidget(m_setInButton);
    reviewRangeRow->addWidget(m_setOutButton);
    reviewRangeRow->addWidget(m_inOutToggle);
    reviewRangeRow->addSpacing(4);
    reviewRangeRow->addWidget(m_reviewStartFrame);
    reviewRangeRow->addWidget(m_timelineRangeSlider, 1);
    reviewRangeRow->addWidget(m_reviewEndFrame);
    column->addLayout(reviewRangeRow);

    m_transport = new TransportControls(m_commands, central);
    m_transport->setVolumePercent(qRound(m_playback->volume() * 100));
    m_transport->setMuted(m_playback->isMuted());
    connect(m_transport, &TransportControls::volumeChanged, this, [this](int value) {
        m_playback->setVolume(value / 100.0); m_settings->setVolume(value / 100.0);
        if (m_volumeSlider && m_volumeSlider->value() != value) m_volumeSlider->setValue(value);
    });
    connect(m_transport, &TransportControls::playbackSpeedRequested, this,
            [this](double speed) { m_playback->setPlaybackSpeed(speed); });
    connect(m_transport, &TransportControls::zoomPercentRequested, this, &MainWindow::zoomViewerToPercent);
    column->addWidget(m_transport);

    // The player is one page; the Review Library, built when first shown, is
    // the other.
    m_playerPage = central;
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("MainPages"));
    m_pages->addWidget(central);
    setCentralWidget(m_pages);

    // --- Left dock: sources ----------------------------------------------
    m_sources = new SourcesPanel(this);
    m_sources->setProject(m_project.get());
    // Delivered on the GUI thread (queued); a clip removed meanwhile is ignored.
    connect(m_thumbnailWorker, &media::ThumbnailWorker::thumbnailReady, m_sources,
            [this](QUuid id, quint64, const QImage& image, const QString& error, bool) {
                if (const auto preview = m_previewThumbnailRequests.find(id); preview != m_previewThumbnailRequests.end()) {
                    if (!image.isNull() && m_libraryPage) m_libraryPage->setThumbnail(*preview, image);
                    m_previewThumbnailRequests.erase(preview);
                    return;
                }
                if (!error.isEmpty()) qCDebug(log::ui) << "No thumbnail for" << id << ":" << error;
                if (!image.isNull() && m_project->indexForId(id) >= 0) m_sources->setThumbnail(id, image);
            });
    connect(m_sources, &SourcesPanel::thumbnailWanted, this, &MainWindow::requestThumbnail);

    m_sourcesDock = new QDockWidget(tr("Playlist"), this);
    m_sourcesDock->setObjectName(QStringLiteral("SourcesDock"));
    m_sourcesDock->setWidget(m_sources);
    m_sourcesDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_sourcesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    // The panel draws its own SOURCES heading, so the dock's title bar would be
    // a second one. A zero-height title widget removes it without losing the
    // dock -- an empty QWidget alone still claims its layout row.
    auto* emptyTitleBar = new QWidget(m_sourcesDock);
    emptyTitleBar->setFixedHeight(0);
    m_sourcesDock->setTitleBarWidget(emptyTitleBar);
    addDockWidget(Qt::LeftDockWidgetArea, m_sourcesDock);

    m_bookmarks = new BookmarkPanel(this);
    m_bookmarks->setModel(m_timeline.get());
    m_bookmarksDock = new QDockWidget(tr("Bookmarks"), this);
    m_bookmarksDock->setObjectName(QStringLiteral("BookmarksDock"));
    m_bookmarksDock->setWidget(m_bookmarks);
    m_bookmarksDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_bookmarksDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, m_bookmarksDock);

    // --- Notes (right side, like SyncSketch) -------------------------------
    m_notesPanel = new NotesPanel(this);
    m_notesPanel->setReviewerName(m_settings->reviewerName());
    m_notesDock = new QDockWidget(tr("Notes"), this);
    m_notesDock->setObjectName(QStringLiteral("NotesDock"));
    m_notesDock->setWidget(m_notesPanel);
    m_notesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_notesDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, m_notesDock);
    // Starts closed so the picture gets the room; F6 or the Notes button at
    // the top right opens it.
    m_notesDock->hide();

    // --- Review header: project › clip, status, export --------------------
    // A fixed toolbar row across the top, above the drawing tools, so it never
    // disturbs the central column that full screen and compare rearrange.
    m_reviewHeader = new ReviewHeaderBar(this);
    auto* headerBar = new QToolBar(tr("Review"), this);
    m_reviewHeaderToolbar = headerBar;
    headerBar->setObjectName(QStringLiteral("ReviewHeaderToolbar"));
    headerBar->setMovable(false);
    headerBar->setFloatable(false);
    headerBar->toggleViewAction()->setVisible(false);
    headerBar->addWidget(m_reviewHeader);
    addToolBar(Qt::TopToolBarArea, headerBar);
    addToolBarBreak(Qt::TopToolBarArea);
    connect(m_reviewHeader, &ReviewHeaderBar::libraryRequested, this, &MainWindow::showReviewLibrary);
    connect(m_reviewHeader, &ReviewHeaderBar::statusChosen, this, [this](project::ClipStatus status) {
        const int index = m_project->activeIndex();
        if (index >= 0) m_project->setClipStatus(m_project->entries().at(index).id, status);
    });
    connect(m_reviewHeader, &ReviewHeaderBar::showInFolderRequested, this, [this] {
        const int index = m_project->activeIndex();
        if (index < 0 || !m_project->entries().at(index).source) return;
        const QString path = m_project->entries().at(index).source->filePath();
        if (!platform::PlatformInfo::revealInFileManager(path))
            statusBar()->showMessage(tr("Could not open the folder %1")
                                         .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())),
                                     4000);
    });
    connect(m_project.get(), &project::Project::entriesChanged, this, &MainWindow::refreshReviewHeader);
    connect(m_project.get(), &project::Project::entriesChanged, this, [this] {
        if (QAction* action = m_commands->action(CommandId::ExportReviewNotesBatch))
            action->setEnabled(!m_project->entries().isEmpty() && !notesPdfInProgress());
    });
    connect(m_project.get(), &project::Project::activeIndexChanged, this, [this](int) { refreshReviewHeader(); });
    connect(m_project.get(), &project::Project::nameChanged, this, [this](const QString&) { refreshReviewHeader(); });
    connect(m_project.get(), &project::Project::clipStatusChanged, this, [this](const QUuid&) { refreshReviewHeader(); });
    refreshReviewHeader();

    // --- Drawing toolbar --------------------------------------------------
    // Shown only while Draw on Frame is on.
    m_annotationToolbar = new AnnotationToolbar(this);
    m_annotationToolbar->setBrushSets(m_brushLibrary->sets());
    m_annotationToolbar->setSettings(m_settings->annotationTool());
    addToolBar(Qt::TopToolBarArea, m_annotationToolbar);
    m_annotationToolbar->hide();
    m_viewer->setAnnotationTool(m_annotationToolbar->settings());
    // Floats over the central column, beside the picture, not in its layout;
    // the viewer itself never knows it is there.
    m_brushSizePill = new BrushSizePill(m_viewer->parentWidget());
    m_brushSizePill->hide();
    m_annotationToolbar->attachSizePill(m_brushSizePill);
    // Tracing paper and ghosts sit with the other view tools under the timeline.
    m_transport->addViewTools(m_annotationToolbar->viewTools());
    m_viewer->installEventFilter(this);
    m_annotationToolbar->installEventFilter(this);

    // --- Status bar -------------------------------------------------------
    m_statusInfo = new StatusInfoBar(this);
    m_statusInfo->setModel(m_timeline.get());
    statusBar()->addPermanentWidget(m_statusInfo, 1);
    m_viewerZoomStatus = new QLabel(tr("Fit"), this);
    m_viewerZoomStatus->setObjectName(QStringLiteral("ViewerZoomStatus"));
    m_viewerZoomStatus->setMinimumWidth(72);
    m_viewerZoomStatus->setAlignment(Qt::AlignCenter);
    m_viewerZoomStatus->setProperty("atkRole", "statusCaption");
    statusBar()->addPermanentWidget(m_viewerZoomStatus);
    statusBar()->setSizeGripEnabled(true);
    // The readout fills the bar, leaving QStatusBar no room to draw messages,
    // so they are shown in the readout. The Review Library hides the bar; it
    // shows it again while a message is up, so a save there still says so.
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString& message) {
        m_statusInfo->setMessage(message);
        if (isShowingReviewLibrary()) statusBar()->setVisible(!message.isEmpty());
    });
}

void MainWindow::buildMenus()
{
    // Adding every action to the window itself gives the shortcuts
    // window-wide scope, so they fire regardless of which widget has focus.
    addActions(m_commands->allActions());

    const commands::CommandCategory categories[] = {
        commands::CommandCategory::File,
        commands::CommandCategory::Edit,
        commands::CommandCategory::Playback,
        commands::CommandCategory::Audio,
        commands::CommandCategory::View,
        commands::CommandCategory::Help,
    };

    m_commands->action(CommandId::OpenMedia)->setIcon(icons::icon(QStringLiteral("folder-open")));
    m_commands->action(CommandId::Preferences)->setIcon(icons::icon(QStringLiteral("settings")));
    m_commands->action(CommandId::AddBookmark)->setIcon(icons::icon(QStringLiteral("bookmark-plus")));
    m_commands->action(CommandId::TimelineZoomFit)->setIcon(icons::icon(QStringLiteral("move-horizontal")));
    m_commands->action(CommandId::ZoomFit)->setIcon(icons::icon(QStringLiteral("scan")));
    m_commands->action(CommandId::ZoomActualSize)->setIcon(icons::icon(QStringLiteral("zoom-in")));
    m_commands->action(CommandId::UndoAnnotation)->setIcon(icons::icon(QStringLiteral("undo-2")));
    m_commands->action(CommandId::RedoAnnotation)->setIcon(icons::icon(QStringLiteral("redo-2")));
    m_commands->action(CommandId::ClearFrameAnnotations)->setIcon(icons::icon(QStringLiteral("trash-2")));
    m_commands->action(CommandId::CopyAnnotations)->setIcon(icons::icon(QStringLiteral("copy")));
    m_commands->action(CommandId::CutAnnotations)->setIcon(icons::icon(QStringLiteral("scissors")));
    m_commands->action(CommandId::PasteAnnotations)->setIcon(icons::icon(QStringLiteral("clipboard-paste")));
    m_commands->action(CommandId::CopyAnnotations)
        ->setToolTip(tr("Copy the selected drawing, or everything on this frame"));
    m_commands->action(CommandId::CutAnnotations)
        ->setToolTip(tr("Cut the selected drawing, or everything on this frame"));
    m_commands->action(CommandId::PasteAnnotations)
        ->setToolTip(tr("Paste copied drawings and notes onto this frame"));
    m_commands->action(CommandId::RenameFromRoster)->setIcon(icons::icon(QStringLiteral("text-cursor-input")));
    m_commands->action(CommandId::SaveProject)->setIcon(icons::icon(QStringLiteral("save")));
    m_commands->action(CommandId::SaveProjectAs)->setIcon(icons::icon(QStringLiteral("save-all")));
    m_reviewHeader->setSaveActions(m_commands->action(CommandId::SaveProject),
                                   m_commands->action(CommandId::SaveProjectAs));
    m_reviewHeader->setRenameAction(m_commands->action(CommandId::RenameFromRoster));
    m_reviewHeader->setExportActions({m_commands->action(CommandId::ExportReview),
                                      m_commands->action(CommandId::ExportReviewNotes),
                                      m_commands->action(CommandId::ExportNotesVideo),
                                      m_commands->action(CommandId::ExportReviewNotesBatch),
                                      m_commands->action(CommandId::ExportCurrentFrame)});
    // Undo, redo, clear, copy, cut and paste also sit on the drawing row, as
    // the same commands.
    m_annotationToolbar->addEditActions({m_commands->action(CommandId::UndoAnnotation),
                                         m_commands->action(CommandId::RedoAnnotation),
                                         m_commands->action(CommandId::ClearFrameAnnotations),
                                         m_commands->action(CommandId::CopyAnnotations),
                                         m_commands->action(CommandId::CutAnnotations),
                                         m_commands->action(CommandId::PasteAnnotations)});
    // Flip canvas, as a quick button on the drawing row too.
    m_annotationToolbar->addEditActions({m_commands->action(CommandId::FlipHorizontal)});

    for (const commands::CommandCategory category : categories) {
        QMenu* menu = menuBar()->addMenu(commands::categoryTitle(category));

        bool first = true;
        for (const commands::CommandDefinition& definition : commands::allCommands()) {
            if (definition.category != category) {
                continue;
            }
            if (!first && startsNewGroup(definition.id)) {
                menu->addSeparator();
            }
            if (QAction* action = m_commands->action(definition.id)) {
                menu->addAction(action);
            }
            first = false;
        }
    }

    // Reflect state that the window owns rather than the action.
    if (QAction* sourcesAction = m_commands->action(CommandId::ToggleSourcesPanel)) {
        sourcesAction->setChecked(true);
    }
    if (QAction* fileAction = menuBar()->actions().value(0); fileAction && fileAction->menu()) {
        m_recentProjectsMenu = fileAction->menu()->addMenu(tr("Recent Projects"));
        refreshRecentProjectsMenu();
    }
    if (QAction* bookmarksAction = m_commands->action(CommandId::ToggleBookmarksPanel)) {
        bookmarksAction->setChecked(false);
    }
    if (QAction* snapAction = m_commands->action(CommandId::ToggleBookmarkSnap)) {
        snapAction->setChecked(m_settings->bookmarkSnapEnabled());
        m_timelineWidget->setBookmarkSnapEnabled(m_settings->bookmarkSnapEnabled());
    }
    if (QAction* showDrawings = m_commands->action(CommandId::ToggleAnnotationsVisible)) {
        showDrawings->setChecked(true);
    }
    m_tracingPaperLevel = m_settings->tracingPaperLevel();
    applyTracingPaper(m_settings->tracingPaper());
    applyGhostSettings(m_settings->ghostSettings());
    {
        auto* holdModes = new QActionGroup(this);
        holdModes->setExclusive(true);
        for (const CommandId id : {CommandId::HoldDrawingsAddToCurrent, CommandId::HoldDrawingsAddNew,
                                   CommandId::HoldDrawingsOff})
            if (QAction* action = m_commands->action(id)) holdModes->addAction(action);
        m_holdOnMode = m_settings->holdDrawingsOnMode();
        applyHoldMode(m_settings->holdDrawings(), false);
    }
    if (QAction* dragMarkers = m_commands->action(CommandId::ToggleDragMarkers)) {
        dragMarkers->setChecked(m_settings->dragMarkersEnabled());
        m_timelineWidget->setMarkerDragEnabled(m_settings->dragMarkersEnabled());
    }
    if (QAction* notesAction = m_commands->action(CommandId::ToggleNotesPanel)) {
        notesAction->setChecked(false);
        notesAction->setIcon(icons::icon(QStringLiteral("message-square")));
        // Top right of the menu bar, above where the panel opens.
        auto* notesButton = new QToolButton(menuBar());
        notesButton->setObjectName(QStringLiteral("NotesToggleButton"));
        notesButton->setDefaultAction(notesAction);
        notesButton->setText(tr("Notes"));
        notesButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        notesButton->setIconSize(QSize(theme::metrics::iconSmall, theme::metrics::iconSmall));
        notesButton->setFocusPolicy(Qt::NoFocus);
        notesButton->setProperty("atkRole", "iconButton");
        const auto refreshTip = [notesButton, notesAction] {
            notesButton->setText(tr("Notes"));
            const QString shortcut = notesAction->shortcut().toString(QKeySequence::NativeText);
            notesButton->setToolTip(shortcut.isEmpty() ? tr("Show or hide the notes panel")
                                                       : tr("Show or hide the notes panel (%1)").arg(shortcut));
        };
        connect(notesAction, &QAction::changed, notesButton, refreshTip);
        refreshTip();
        menuBar()->setCornerWidget(notesButton, Qt::TopRightCorner);
    }
    if (QAction* scrubAction = m_commands->action(CommandId::ToggleAudioScrub)) {
        scrubAction->setChecked(m_settings->audioScrubEnabled());
    }
    if (QAction* stepAction = m_commands->action(CommandId::ToggleFrameStepAudio)) {
        stepAction->setChecked(m_settings->frameStepAudioEnabled());
    }
    if (QAction* muteAction = m_commands->action(CommandId::ToggleMute)) {
        muteAction->setChecked(m_settings->muted());
    }
    if (QAction* videoFullScreen = m_commands->action(CommandId::ToggleVideoFullScreen)) {
        videoFullScreen->setEnabled(false);
    }
    m_commands->action(CommandId::CompareSideBySide)->setChecked(true);
    m_commands->action(CommandId::CompareSideBySide)->setEnabled(false);
    m_commands->action(CommandId::CompareStacked)->setEnabled(false);
    m_commands->action(CommandId::CompareWipe)->setEnabled(false);
    m_commands->action(CommandId::CompareBlend)->setEnabled(false);
    m_commands->action(CommandId::CompareDifference)->setEnabled(false);
    QAction* clearMarks = m_commands->action(CommandId::ClearRange);
    for (const auto& [button, id] : {std::pair{m_setInButton, CommandId::SetRangeIn},
                                     std::pair{m_setOutButton, CommandId::SetRangeOut}}) {
        QAction* action = m_commands->action(id);
        if (!action) continue;
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button, [button, action] { button->setEnabled(action->isEnabled()); });
        button->setEnabled(action->isEnabled());
        if (!clearMarks) continue;
        connect(button, &QWidget::customContextMenuRequested, this, [button, clearMarks](const QPoint& at) {
            QMenu menu(button);
            menu.addAction(clearMarks);
            menu.exec(button->mapToGlobal(at));
        });
    }
    if (QAction* toggle = m_commands->action(CommandId::ToggleInOut)) {
        connect(m_inOutToggle, &QToolButton::clicked, toggle, &QAction::trigger);
        connect(m_inOutToggle, &QWidget::customContextMenuRequested, this, [this, clearMarks](const QPoint& at) {
            if (!clearMarks) return;
            QMenu menu(m_inOutToggle);
            menu.addAction(clearMarks);
            menu.exec(m_inOutToggle->mapToGlobal(at));
        });
    }
    syncInOutButtons();
}

void MainWindow::connectSignals()
{
    connect(m_commands, &CommandRegistry::commandTriggered,
            this, &MainWindow::onCommand);

    connect(m_viewer, &ViewerWidget::zoomChanged, this,
            [this](qreal percent, bool fitMode) {
        const QString value = tr("%1%").arg(qRound(percent));
        m_viewerZoomStatus->setText(fitMode ? tr("Fit %1").arg(value) : value);
        m_transport->setZoomText(fitMode ? tr("Fit") : value);
    });
    // Pane B zooms on its own in side-by-side and stacked comparison; the
    // readout follows it while it is the active pane.
    connect(m_viewerB, &ViewerWidget::zoomChanged, this, [this](qreal percent, bool fitMode) {
        if (activeViewer() != m_viewerB) return;
        const QString value = tr("%1%").arg(qRound(percent));
        m_viewerZoomStatus->setText(fitMode ? tr("Fit %1").arg(value) : value);
        m_transport->setZoomText(fitMode ? tr("Fit") : value);
    });
    connect(m_playback.get(), &playback::PlaybackController::playbackSpeedChanged, this, [this](double speed) {
        refreshPlaybackSpeed();
        statusBar()->showMessage(speed == 1.0 ? tr("Normal speed")
                                              : tr("Playing at %1 (%2x), without sound")
                                                    .arg(TransportControls::speedLabel(speed, m_timeline->frameRate()),
                                                         QLocale::c().toString(speed, 'g', 3)),
                                 2500);
    });
    connect(m_timeline.get(), &timeline::TimelineModel::frameRateChanged, this, [this] { refreshPlaybackSpeed(); });
    connect(m_playback.get(), &playback::PlaybackController::mediaOpened, this, [this] { refreshPlaybackSpeed(); });
    connect(m_playback.get(), &playback::PlaybackController::mediaClosed, this, [this] { refreshPlaybackSpeed(); });
    connect(m_timeline.get(), &timeline::TimelineModel::frameCountChanged, this, [this] { refreshHoldSpans(); });
    refreshPlaybackSpeed();
    m_viewer->resetNavigationToFit();

    connect(m_playback.get(), &playback::PlaybackController::stateChanged,
            this, &MainWindow::onPlayerStateChanged);
    connect(m_playback.get(), &playback::PlaybackController::authoritativeNavigationStarted,
            m_timelineWidget, &TimelineWidget::followAuthoritativeFrame);
    connect(m_sources, &SourcesPanel::addMediaRequested, this, &MainWindow::addMediaDialog);
    connect(m_sources, &SourcesPanel::removeRequested, this, &MainWindow::removePlaylistIndex);
    connect(m_sources, &SourcesPanel::moveRequested, this, &MainWindow::movePlaylistIndex);
    connect(m_sources, &SourcesPanel::arrangeRequested, this, &MainWindow::arrangePlaylist);
    connect(m_sources, &SourcesPanel::categoryRequested, this, [this](int index, const QString& category) {
        if (index < 0 || index >= m_project->entries().size()) return;
        if (m_project->setClipCategory(m_project->entries().at(index).id, category)) updateWindowTitle();
    });
    connect(m_sources, &SourcesPanel::relinkRequested, this, [this](int) { relinkSelectedMedia(); });
    connect(m_sources, &SourcesPanel::frameRateRequested, this, &MainWindow::changeSourceFrameRate);
    connect(m_sources, &SourcesPanel::sourceActivated, this,
            [this](int index) { activatePlaylistIndex(index); });

    // Decoded frames reach the viewer through the controller, so the viewer
    // never talks to the decoder and never decodes inside a paint event.
    connect(m_playback.get(), &playback::PlaybackController::frameChanged,
            this, [this](const media::VideoFrame& frame) {
                m_viewer->setFrame(frame);
                if (frame.frameIndex != m_displayedFrameIndex
                    || frame.image.height() != m_displayedImageHeight) {
                    m_displayedFrameIndex = frame.frameIndex;
                    m_displayedImageHeight = frame.image.height();
                    m_displayedFramePtsUs = frame.ptsUs;
                    refreshViewerAnnotations();
                    m_notesPanel->setCurrentFrame(
                        frame.frameIndex, timeline::timecode::fromFrame(frame.frameIndex, m_timeline->frameRate()),
                        !m_annotationSourceId.isNull());
                }
                m_compareComposite->setFrameA(frame);
                synchronizeComparison(frame);
            });

    // --- Drawing on frames ------------------------------------------------
    connect(m_viewer, &ViewerWidget::annotationItemDrawn, this,
            [this](qint64 frameIndex, const annotation::AnnotationItem& item) {
                if (m_annotationSourceId.isNull() || frameIndex < 0) return;
                frameIndex = annotationEditFrame(frameIndex, true);
                if (m_annotations->addItem(frameIndex, annotationMediaTimeUs(frameIndex), item) == 0)
                    statusBar()->showMessage(tr("This drawing could not be added (frame limit reached)."), 4000);
            });
    // One eraser swipe is one undo step.
    connect(m_viewer, &ViewerWidget::annotationEraseStarted, this,
            [this](qint64) { m_annotations->beginEditGroup(); });
    connect(m_viewer, &ViewerWidget::annotationEraseFinished, this,
            [this] { m_annotations->endEditGroup(); });
    connect(m_viewer, &ViewerWidget::annotationEraseRequested, this,
            [this](qint64 frameIndex, const QVector<QPointF>& centers, qreal radius, const QSizeF& imageSize) {
                m_annotations->eraseDiscs(annotationEditFrame(frameIndex, false), centers, radius, imageSize,
                                          m_annotationToolbar->settings().eraserMode);
            });
    connect(m_viewer, &ViewerWidget::annotationItemChanged, this,
            [this](qint64 frameIndex, const annotation::AnnotationItem& item) {
                m_annotations->replaceItem(annotationEditFrame(frameIndex, false), item);
            });
    connect(m_viewer, &ViewerWidget::annotationItemDeleteRequested, this,
            [this](qint64 frameIndex, quint64 id) {
                m_annotations->removeItem(annotationEditFrame(frameIndex, false), id);
            });
    connect(m_annotationToolbar, &AnnotationToolbar::settingsChanged, this, &MainWindow::applyAnnotationTool);
    connect(m_annotationToolbar, &AnnotationToolbar::importBrushesRequested, this, &MainWindow::importBrushSet);

    // Notes panel: writes go through the same store as drawings, so they
    // share undo, saving and export.
    connect(m_notesPanel, &NotesPanel::addNoteRequested, this, [this](qint64 frame, const QString& text) {
        if (m_annotationSourceId.isNull() || frame < 0) return;
        annotation::FrameNote note;
        note.text = text;
        note.author = m_notesPanel->reviewerName();
        note.createdMsUtc = QDateTime::currentMSecsSinceEpoch();
        if (m_annotations->addNote(frame, annotationMediaTimeUs(frame), note) == 0)
            statusBar()->showMessage(tr("The note could not be added."), 4000);
    });
    connect(m_notesPanel, &NotesPanel::updateNoteRequested, this,
            [this](qint64 frame, const annotation::FrameNote& note) { m_annotations->updateNote(frame, note); });
    connect(m_notesPanel, &NotesPanel::deleteNoteRequested, this,
            [this](qint64 frame, quint64 id) { m_annotations->removeNote(frame, id); });
    connect(m_notesPanel, &NotesPanel::copyNoteRequested, this, &MainWindow::copyNote);
    connect(m_timelineWidget, &TimelineWidget::annotatedFrameDropped, this, &MainWindow::moveAnnotatedFrame);
    connect(m_timelineWidget, &TimelineWidget::annotatedFramesDropped, this, &MainWindow::moveAnnotatedFrames);
    connect(m_notesPanel, &NotesPanel::noteActivated, this, [this](qint64 frame) {
        if (frame != m_displayedFrameIndex) m_playback->seekFrame(frame);
    });
    connect(m_notesPanel, &NotesPanel::reviewerNameChanged, this,
            [this](const QString& name) { m_settings->setReviewerName(name); });
    connect(m_notesPanel, &NotesPanel::exportPdfRequested, this, &MainWindow::exportReviewNotes);
    connect(m_notesPanel, &NotesPanel::exportVideoRequested, this, [this] { exportReview(true); });
    connect(m_notesDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (QAction* action = m_commands->action(CommandId::ToggleNotesPanel)) {
            QSignalBlocker blocker(action);
            action->setChecked(visible);
        }
    });
    connect(m_annotations.get(), &annotation::AnnotationStore::annotationsEdited, this, &MainWindow::refreshNotesPanel);
    connect(m_annotations.get(), &annotation::AnnotationStore::annotationsReset, this, &MainWindow::refreshNotesPanel);
    connect(m_annotationToolbar, &AnnotationToolbar::removeBrushSetRequested, this, &MainWindow::removeBrushSet);
    connect(m_annotationToolbar, &AnnotationToolbar::tracingPaperRequested, this, &MainWindow::applyTracingPaper);
    connect(m_annotationToolbar, &AnnotationToolbar::ghostSettingsRequested, this, &MainWindow::applyGhostSettings);
    connect(m_annotations.get(), &annotation::AnnotationStore::annotationsEdited, this, [this] {
        if (!m_annotationSourceId.isNull())
            m_project->setAnnotations(m_annotationSourceId, m_annotations->annotations());
    });
    connect(m_annotations.get(), &annotation::AnnotationStore::frameChanged, this, [this](qint64 frame) {
        // Ghosts show nearby frames too, and a held drawing comes from an
        // earlier frame, so then any edit may change what is on screen.
        if (frame == m_displayedFrameIndex || m_ghostSettings.enabled || m_holdMode != annotation::HoldMode::Off)
            refreshViewerAnnotations();
    });
    connect(m_annotations.get(), &annotation::AnnotationStore::annotationsReset, this,
            &MainWindow::refreshViewerAnnotations);
    auto markDrawnFrames = [this] {
        m_timelineWidget->setAnnotatedFrames(m_annotations->annotatedFrames());
        refreshHoldSpans();
    };
    connect(m_annotations.get(), &annotation::AnnotationStore::annotationsEdited, this, markDrawnFrames);
    connect(m_annotations.get(), &annotation::AnnotationStore::annotationsReset, this, markDrawnFrames);
    connect(m_annotations.get(), &annotation::AnnotationStore::historyChanged, this,
            &MainWindow::updateAnnotationActions);
    // Rebind whenever the active source or the playlist changes. Binding is a
    // no-op when the store already holds that source's data, so undo history
    // survives unrelated playlist updates such as probe results.
    connect(m_project.get(), &project::Project::activeIndexChanged, this,
            [this](int) { bindAnnotationsToActiveSource(); });
    connect(m_project.get(), &project::Project::entriesChanged, this,
            &MainWindow::bindAnnotationsToActiveSource);
    updateAnnotationActions();

    connect(m_viewer, &ViewerWidget::activated, this,
            [this] { if (isComparisonActive()) m_compare->setActivePane(playback::ComparePane::A); });
    connect(m_viewerB, &ViewerWidget::activated, this,
            [this] { if (isComparisonActive()) m_compare->setActivePane(playback::ComparePane::B); });
    connect(m_compareComposite, &ComparisonCompositeWidget::zoomChanged, this,
            [this](qreal percent, bool fit) {
                m_viewerZoomStatus->setText(fit ? tr("Fit %1%").arg(qRound(percent)) : tr("%1%").arg(qRound(percent)));
                m_transport->setZoomText(fit ? tr("Fit") : tr("%1%").arg(qRound(percent)));
            });
    connect(m_compareBar, &CompareBar::sourceASelected, this, &MainWindow::selectComparisonSourceA);
    connect(m_compareBar, &CompareBar::sourceBSelected, this, &MainWindow::selectComparisonSourceB);
    connect(m_compareBar, &CompareBar::audioModeSelected, this, &MainWindow::applyComparisonAudioMode);
    connect(m_compareBar, &CompareBar::viewModeSelected, m_compare.get(), &playback::CompareSession::setLayout);
    connect(m_compareBar, &CompareBar::sourceBOffsetFramesChanged, this, &MainWindow::setSourceBOffsetFrames);
    connect(m_compareBar, &CompareBar::externalAudioOffsetFramesChanged, this, &MainWindow::setExternalAudioOffsetFrames);
    connect(m_compareBar, &CompareBar::wipePositionChanged, m_compare.get(), &playback::CompareSession::setWipePosition);
    connect(m_compareBar, &CompareBar::blendAmountChanged, m_compare.get(), &playback::CompareSession::setBlendAmount);
    connect(m_compareComposite, &ComparisonCompositeWidget::wipePositionChanged, m_compare.get(), &playback::CompareSession::setWipePosition);
    connect(m_compare.get(), &playback::CompareSession::wipePositionChanged, m_compareComposite, &ComparisonCompositeWidget::setWipePosition);
    connect(m_compare.get(), &playback::CompareSession::blendAmountChanged, m_compareComposite, &ComparisonCompositeWidget::setBlendAmount);
    connect(m_compare.get(), &playback::CompareSession::layoutChanged,
            this, &MainWindow::setComparisonLayout);
    connect(m_compare.get(), &playback::CompareSession::activePaneChanged, this,
            [this](playback::ComparePane pane) {
                m_viewer->setStyleSheet(pane == playback::ComparePane::A
                    ? QStringLiteral("border: 2px solid %1;").arg(theme::accent().name()) : QString());
                m_viewerB->setStyleSheet(pane == playback::ComparePane::B
                    ? QStringLiteral("border: 2px solid %1;").arg(theme::accent().name()) : QString());
            });
    connect(m_project.get(), &project::Project::entriesChanged, this, [this] {
        if (!isComparisonActive()) return;
        const int a = m_project->indexForId(m_compare->sourceAId());
        const int b = m_project->indexForId(m_compare->sourceBId());
        if (!sourceUsableForComparison(a)) {
            const int replacementA = m_project->activeIndex();
            const int replacementB = defaultComparisonBIndex(replacementA);
            if (!sourceUsableForComparison(replacementA) || replacementB < 0) exitComparison();
            else if (m_compare->setSources(m_project->entries().at(replacementA).id,
                                           m_project->entries().at(replacementB).id)) {
                activatePlaylistIndex(replacementA);
                openComparisonSourceB();
            } else exitComparison();
        } else if (!sourceUsableForComparison(b)) {
            const int replacementB = defaultComparisonBIndex(a);
            if (replacementB < 0) exitComparison();
            else if (m_compare->setSources(m_compare->sourceAId(),
                                           m_project->entries().at(replacementB).id)) {
                openComparisonSourceB();
            } else exitComparison();
        }
        if (isComparisonActive()
            && m_compare->audioMode() == playback::CompareAudioMode::SourceB)
            applyComparisonAudioMode(playback::CompareAudioMode::SourceB);
        refreshComparisonUi();
    });

    connect(m_playback.get(), &playback::PlaybackController::mediaOpened,
            this, &MainWindow::onMediaOpened);

    // The waveform lives in the controller and is repainted in place; only a
    // repaint request crosses to the widget, never a copy of the peaks.
    connect(m_playback.get(), &playback::PlaybackController::waveformChanged,
            this, [this] { m_timelineWidget->refreshWaveform(); });
    connect(m_playback.get(), &playback::PlaybackController::waveformSourceChanged,
            m_timelineWidget, [this](qint64 offsetUs, quint64) {
                m_timelineWidget->setWaveformTimeOffsetUs(offsetUs);
            });

    // A quiet, self-clearing note rather than a progress bar: analysis is
    // usually over in seconds and the player should not grow a widget for it.
    connect(m_playback.get(), &playback::PlaybackController::waveformAnalysingChanged,
            this, [this](bool analysing) {
                if (analysing) {
                    statusBar()->showMessage(tr("Analyzing audio..."));
                } else {
                    statusBar()->clearMessage();
                }
            });

    connect(m_playback.get(), &playback::PlaybackController::errorOccurred,
            this, &MainWindow::onMediaError);

    connect(m_playback.get(), &playback::PlaybackController::mediaClosed,
            this, [this] {
                exitVideoFullScreen();
                m_viewer->setEmpty();
                m_viewer->setSourceAspectRatio(0.0);
                m_sources->clearCurrentMedia();
                m_statusInfo->clearMediaInfo();
                installPlaceholderTimeline();
                updateTransportEnabled();
                updateWindowTitle();
                refreshMediaInformation();
            });

    connect(m_playback.get(), &playback::PlaybackController::loopEnabledChanged,
            this, [this](bool enabled) {
                // Loop switched on the player itself (the API): follow it.
                if (enabled && m_playlistMode != PlaylistMode::LoopClip) setPlaylistMode(PlaylistMode::LoopClip);
                else if (!enabled && m_playlistMode == PlaylistMode::LoopClip)
                    setPlaylistMode(m_stopAfterEachClip ? PlaylistMode::StopAfterClip : PlaylistMode::PlayThrough);
            });
    {
        auto* modes = new QActionGroup(this);
        modes->setExclusive(true);
        for (const CommandId id : {CommandId::PlaylistPlayThrough, CommandId::PlaylistStopAfterClip,
                                   CommandId::PlaylistLoopClip, CommandId::PlaylistLoopAll})
            if (QAction* action = m_commands->action(id)) modes->addAction(action);
        m_stopAfterEachClip = m_settings->stopAfterEachClip();
        m_loopWholePlaylist = m_settings->loopWholePlaylist();
        // Loop itself starts off each session, as it always has.
        setPlaylistMode(m_stopAfterEachClip ? PlaylistMode::StopAfterClip : PlaylistMode::PlayThrough);
    }

    // Scrubbing the timeline goes through the controller, not straight into the
    // model, so a drag behaves exactly like an API seek.
    connect(m_timelineWidget, &TimelineWidget::scrubStarted,
            m_playback.get(), &playback::PlaybackController::beginScrub);
    connect(m_timelineWidget, &TimelineWidget::scrubPreviewRequested,
            m_playback.get(), &playback::PlaybackController::scrubToFrame);
    connect(m_timelineWidget, &TimelineWidget::scrubFinished,
            m_playback.get(), &playback::PlaybackController::endScrub);
    // Drawings keep up with a drag at half resolution, full detail on release.
    connect(m_timelineWidget, &TimelineWidget::scrubStarted, m_viewer, [this] { m_viewer->setScrubbing(true); });
    connect(m_timelineWidget, &TimelineWidget::scrubFinished, m_viewer, [this] { m_viewer->setScrubbing(false); });
    connect(m_frameNumberInput, &FrameNumberInput::seekFrameRequested,
            m_playback.get(), &playback::PlaybackController::seekFrame);
    connect(m_timeline.get(), &timeline::TimelineModel::currentFrameChanged,
            m_frameNumberInput, &FrameNumberInput::setCurrentFrame);

    connect(m_timelineWidget, &TimelineWidget::bookmarkSelected,
            m_bookmarks, &BookmarkPanel::selectBookmark);
    connect(m_timelineWidget, &TimelineWidget::bookmarkActivated,
            this, &MainWindow::activateBookmark);
    connect(m_bookmarks, &BookmarkPanel::bookmarkActivated,
            this, &MainWindow::activateBookmark);
    connect(m_bookmarks, &BookmarkPanel::bookmarkSelected,
            m_timelineWidget, &TimelineWidget::setSelectedBookmark);
    connect(m_bookmarks, &BookmarkPanel::addPointRequested, this, [this] {
        if (QAction* action = m_commands->action(CommandId::AddBookmark)) action->trigger();
    });
    connect(m_bookmarks, &BookmarkPanel::addRangeRequested, this,
            [this](qint64 start, qint64 end) {
                timeline::Bookmark bookmark;
                bookmark.type = timeline::BookmarkType::Range;
                bookmark.frame = start;
                bookmark.endFrame = end;
                const quint64 id = m_timeline->addBookmark(bookmark);
                m_bookmarks->selectBookmark(id);
                statusBar()->showMessage(tr("Range bookmark added: %1–%2")
                    .arg(start + 1).arg(end + 1), 2000);
            });

    connect(m_project.get(), &project::Project::modifiedChanged,
            this, [this](bool) { updateWindowTitle(); });
    connect(m_project.get(), &project::Project::activeIndexChanged,
            this, [this](int) {
                if (m_mediaInformationDialog) m_mediaInformationDialog->clearMediaInformation();
            });
    connect(m_timeline.get(), &timeline::TimelineModel::bookmarksChanged, this, [this] {
        if (m_restoringSourceState || m_project->activeIndex() < 0) return;
        m_project->mutableEntries()[m_project->activeIndex()].bookmarks = m_timeline->bookmarks();
        m_project->setModified(true);
    });
    // Each clip keeps its own in/out marks.
    connect(m_timeline.get(), &timeline::TimelineModel::playbackRangeChanged, this,
            [this](const timeline::PlaybackRange& range) {
                syncInOutButtons();
                if (m_restoringSourceState || m_project->activeIndex() < 0) return;
                m_project->mutableEntries()[m_project->activeIndex()].playbackRange = range;
                m_project->setModified(true);
            });
    connect(m_timeline.get(), &timeline::TimelineModel::frameCountChanged, this, &MainWindow::syncInOutButtons);

    // The frame count only becomes final once media is open, and the transport
    // depends on whether there is an extent at all.
    connect(m_timeline.get(), &timeline::TimelineModel::frameCountChanged,
            this, [this](qint64 count) {
                updateTransportEnabled();
                m_frameNumberInput->setFrameCount(count);
                const int maximum = static_cast<int>(std::max<qint64>(1, count));
                m_reviewStartFrame->setRange(1, maximum);
                m_reviewEndFrame->setRange(1, maximum);
                updateFrameFieldWidth(m_reviewStartFrame, maximum);
                updateFrameFieldWidth(m_reviewEndFrame, maximum);
                refreshMediaInformation();
            });

    const auto refreshReviewFields = [this](qint64 start, qint64 end) {
        QSignalBlocker blockStart(m_reviewStartFrame);
        QSignalBlocker blockEnd(m_reviewEndFrame);
        const int maximum = static_cast<int>(std::max<qint64>(1, m_timeline->frameCount()));
        m_reviewStartFrame->setRange(1, maximum);
        m_reviewEndFrame->setRange(1, maximum);
        // The model is zero-based; all visible frame numbers are one-based.
        m_reviewStartFrame->setValue(static_cast<int>(start + 1));
        m_reviewEndFrame->setValue(static_cast<int>(end + 1));
        const int minimumSpan = static_cast<int>(std::min<qint64>(
            timeline::TimelineViewport::kMinimumVisibleFrames,
            m_timeline->frameCount()));
        m_reviewStartFrame->setMaximum(std::max(1, m_reviewEndFrame->value() - minimumSpan + 1));
        m_reviewEndFrame->setMinimum(std::min(m_reviewEndFrame->maximum(),
            m_reviewStartFrame->value() + minimumSpan - 1));
    };
    connect(m_timeline.get(), &timeline::TimelineModel::viewportChanged,
            this, refreshReviewFields);
    const auto centreStoppedReviewFrame = [this] {
        if (m_playback->isPlaying() || m_timeline->frameCount() <= 0) return;
        const qint64 start = m_timeline->viewport().startFrame();
        const qint64 end = m_timeline->viewport().endFrame();
        // Lower midpoint for even inclusive spans; e.g. 138..149 -> 143.
        m_playback->seekFrame(start + (end - start) / 2);
    };
    connect(m_reviewStartFrame, &QSpinBox::valueChanged, this,
            [this, centreStoppedReviewFrame](int) {
        const qint64 oldSpan = m_timeline->viewport().visibleFrameCount();
        m_timeline->setViewportRange(m_reviewStartFrame->value() - 1,
                                     m_timeline->viewport().endFrame());
        if (m_timeline->viewport().visibleFrameCount() != oldSpan)
            centreStoppedReviewFrame();
    });
    connect(m_reviewEndFrame, &QSpinBox::valueChanged, this,
            [this, centreStoppedReviewFrame](int) {
        const qint64 oldSpan = m_timeline->viewport().visibleFrameCount();
        m_timeline->setViewportRange(m_timeline->viewport().startFrame(),
                                     m_reviewEndFrame->value() - 1);
        if (m_timeline->viewport().visibleFrameCount() != oldSpan)
            centreStoppedReviewFrame();
    });
    connect(m_timelineRangeSlider, &TimelineRangeSlider::rangeResizeCommitted,
            this, centreStoppedReviewFrame);
    connect(m_timelineRangeSlider, &TimelineRangeSlider::fitEntireRequested,
            m_timelineWidget, &TimelineWidget::fitEntire);
    refreshReviewFields(m_timeline->viewport().startFrame(), m_timeline->viewport().endFrame());

    connect(m_sourcesDock, &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
                if (QAction* action = m_commands->action(CommandId::ToggleSourcesPanel)) {
                    QSignalBlocker blocker(action);
                    action->setChecked(visible);
                }
            });
    connect(m_bookmarksDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (QAction* action = m_commands->action(CommandId::ToggleBookmarksPanel)) {
            QSignalBlocker blocker(action);
            action->setChecked(visible);
        }
    });
}

void MainWindow::onCommand(CommandId id, bool checked)
{
    switch (id) {
    // --- Transport: fully wired ------------------------------------------
    case CommandId::PlayPause:
        // Play at the end of a clip goes on to the next one (in every mode:
        // "stop after each clip" pauses between clips, Play moves on).
        if (!isComparisonActive() && !m_playback->isPlaying() && !m_playback->isLoopEnabled()
            && m_project->activeIndex() >= 0
            && m_playback->currentFrame() == m_timeline->effectiveEndFrame()) {
            const int next = nextUsablePlaylistIndex(m_playlistMode == PlaylistMode::LoopPlaylist);
            if (next >= 0 && next != m_project->activeIndex()) {
                activatePlaylistIndex(next, true);
                return;
            }
        }
        m_playback->togglePlayPause();
        return;
    case CommandId::Stop:
        m_playback->stop();
        return;
    case CommandId::PreviousFrame:
        m_playback->stepBackward();
        return;
    case CommandId::NextFrame:
        m_playback->stepForward();
        return;
    case CommandId::FirstFrame:
        m_playback->goToStart();
        return;
    case CommandId::LastFrame:
        m_playback->goToEnd();
        return;
    case CommandId::ToggleLoop:
        if (checked) setPlaylistMode(m_loopWholePlaylist ? PlaylistMode::LoopPlaylist : PlaylistMode::LoopClip);
        else setPlaylistMode(m_stopAfterEachClip ? PlaylistMode::StopAfterClip : PlaylistMode::PlayThrough);
        return;
    case CommandId::PlaylistPlayThrough:
        setPlaylistMode(PlaylistMode::PlayThrough); return;
    case CommandId::PlaylistStopAfterClip:
        setPlaylistMode(PlaylistMode::StopAfterClip); return;
    case CommandId::PlaylistLoopClip:
        setPlaylistMode(PlaylistMode::LoopClip); return;
    case CommandId::PlaylistLoopAll:
        setPlaylistMode(PlaylistMode::LoopPlaylist); return;
    case CommandId::SkipBack10Seconds:
        m_playback->skipBySeconds(-10); return;
    case CommandId::SkipForward10Seconds:
        m_playback->skipBySeconds(10); return;
    case CommandId::DecreasePlaybackSpeed:
        m_playback->decreasePlaybackSpeed(); return;
    case CommandId::IncreasePlaybackSpeed:
        m_playback->increasePlaybackSpeed(); return;
    case CommandId::ResetPlaybackSpeed:
        m_playback->setPlaybackSpeed(1.0); return;
    case CommandId::PreviousPlaylistItem:
        activatePlaylistIndex(m_project->activeIndex() - 1, m_playback->isPlaying()); return;
    case CommandId::NextPlaylistItem:
        activatePlaylistIndex(m_project->activeIndex() + 1, m_playback->isPlaying()); return;
    case CommandId::RemovePlaylistItem:
        removePlaylistIndex(m_sources->selectedIndex()); return;
    case CommandId::MovePlaylistItemUp:
        movePlaylistIndex(m_sources->selectedIndex(), m_sources->selectedIndex() - 1); return;
    case CommandId::MovePlaylistItemDown:
        movePlaylistIndex(m_sources->selectedIndex(), m_sources->selectedIndex() + 1); return;
    case CommandId::RelinkMedia:
        relinkSelectedMedia(); return;

    // --- Range and bookmarks: fully wired ---------------------------------
    case CommandId::SetRangeIn:
        m_timeline->setRangeInAtCurrentFrame();
        statusBar()->showMessage(tr("In point set at frame %1").arg(m_timeline->currentFrame() + 1), 2000);
        return;
    case CommandId::SetRangeOut:
        m_timeline->setRangeOutAtCurrentFrame();
        statusBar()->showMessage(tr("Out point set at frame %1").arg(m_timeline->currentFrame() + 1), 2000);
        return;
    case CommandId::ClearRange:
        m_timeline->clearPlaybackRange();
        statusBar()->showMessage(tr("In and out points cleared: the whole clip plays"), 2000);
        return;
    case CommandId::ToggleInOut:
        m_timeline->setPlaybackMarksEnabled(checked);
        syncInOutButtons();
        if (m_timeline->hasPlaybackMarks())
            statusBar()->showMessage(tr("In and out points on: playing frames %1 to %2")
                                         .arg(m_timeline->playbackRange().startFrame + 1)
                                         .arg(m_timeline->playbackRange().endFrame + 1), 3000);
        else if (m_timeline->hasKeptMarks())
            statusBar()->showMessage(tr("In and out points off: playing the whole clip"), 3000);
        else
            statusBar()->showMessage(tr("No in and out points yet. Press [ and ] to set them."), 3000);
        return;
    case CommandId::AddBookmark: {
        timeline::Bookmark bookmark;
        bookmark.frame = m_timeline->currentFrame();
        bookmark.endFrame = bookmark.frame;
        const quint64 bookmarkId = m_timeline->addBookmark(bookmark);
        m_bookmarks->selectBookmark(bookmarkId);
        statusBar()->showMessage(tr("Bookmark added at frame %1").arg(bookmark.frame + 1), 2000);
        return;
    }
    case CommandId::AddRangeBookmark: {
        m_bookmarksDock->show();
        m_bookmarks->useCurrentReviewRange();
        statusBar()->showMessage(tr("Enter or confirm range bounds in the Bookmarks panel"), 2000);
        return;
    }
    case CommandId::NextBookmark: {
        if (const timeline::Bookmark* bookmark = m_timeline->nextBookmark(m_timeline->currentFrame()))
            activateBookmark(bookmark->id);
        return;
    }
    case CommandId::PreviousBookmark: {
        if (const timeline::Bookmark* bookmark = m_timeline->previousBookmark(m_timeline->currentFrame()))
            activateBookmark(bookmark->id);
        return;
    }
    case CommandId::DeleteBookmark: {
        const quint64 selected = m_bookmarks->selectedBookmarkId();
        if (selected) m_timeline->removeBookmark(selected);
        else m_timeline->removeBookmarkAt(m_timeline->currentFrame());
        return;
    }
    case CommandId::ToggleBookmarkSnap:
        m_timelineWidget->setBookmarkSnapEnabled(checked);
        m_settings->setBookmarkSnapEnabled(checked);
        return;

    // --- Drawing ------------------------------------------------------------
    case CommandId::ToggleAnnotateMode:
        setAnnotateMode(checked);
        return;
    case CommandId::UndoAnnotation:
        m_annotations->undo();
        return;
    case CommandId::RedoAnnotation:
        m_annotations->redo();
        return;
    case CommandId::ClearFrameAnnotations:
        // A held drawing on screen is cleared where it was drawn.
        if (m_displayedFrameIndex >= 0) m_annotations->clearFrame(annotationEditFrame(m_displayedFrameIndex, false));
        return;
    case CommandId::CopyAnnotations:
    case CommandId::CutAnnotations:
        copyAnnotations(id == CommandId::CutAnnotations);
        return;
    case CommandId::PasteAnnotations:
        pasteAnnotations();
        return;
    case CommandId::IncreaseBrushSize:
    case CommandId::DecreaseBrushSize: {
        auto settings = m_annotationToolbar->settings();
        settings.setActiveSizePixels(annotation::stepBrushSize(settings.activeSizePixels(),
                                                               id == CommandId::IncreaseBrushSize ? 1 : -1));
        m_annotationToolbar->setSettings(settings);
        applyAnnotationTool(settings);
        statusBar()->showMessage(settings.tool == annotation::AnnotationTool::Eraser
                                     ? tr("Eraser size %1 px").arg(settings.eraserSizePixels)
                                     : tr("Brush size %1 px").arg(settings.sizePixels), 1500);
        return;
    }
    case CommandId::ToggleNotesPanel:
        m_notesDock->setVisible(checked);
        if (checked) m_notesDock->raise();
        return;
    case CommandId::ToggleAnnotationsVisible:
        m_viewer->setAnnotationsVisible(checked);
        return;
    case CommandId::ToggleTracingPaper:
        applyTracingPaper(checked ? m_tracingPaperLevel : 0);
        statusBar()->showMessage(checked ? tr("Tracing paper %1%").arg(m_tracingPaperLevel) : tr("Tracing paper off"), 1500);
        return;
    case CommandId::ToggleGhosting: {
        auto ghost = m_ghostSettings;
        ghost.enabled = checked;
        applyGhostSettings(ghost);
        statusBar()->showMessage(checked ? tr("Ghosting %1 frames back, %2 forward").arg(ghost.before).arg(ghost.after)
                                         : tr("Ghosting off"), 1500);
        return;
    }
    case CommandId::ToggleHoldDrawings:
        applyHoldMode(checked ? m_holdOnMode : annotation::HoldMode::Off);
        return;
    case CommandId::HoldDrawingsAddToCurrent:
        applyHoldMode(annotation::HoldMode::AddToCurrent);
        return;
    case CommandId::HoldDrawingsAddNew:
        applyHoldMode(annotation::HoldMode::AddNew);
        return;
    case CommandId::HoldDrawingsOff:
        applyHoldMode(annotation::HoldMode::Off);
        return;
    case CommandId::ToggleDragMarkers:
        m_timelineWidget->setMarkerDragEnabled(checked);
        m_settings->setDragMarkersEnabled(checked);
        statusBar()->showMessage(checked ? tr("Drag markers on: drag a marker under the timeline to move that "
                                              "frame's notes and drawings; hold Ctrl to copy.")
                                         : tr("Drag markers off"),
                                 3000);
        return;
    case CommandId::NextAnnotatedFrame:
        seekAnnotatedFrame(1);
        return;
    case CommandId::PreviousAnnotatedFrame:
        seekAnnotatedFrame(-1);
        return;

    // --- View: fully wired ------------------------------------------------
    case CommandId::ZoomFit:
        if (isComparisonActive() && m_compare->layout() != playback::CompareLayout::SideBySide
            && m_compare->layout() != playback::CompareLayout::Stacked) m_compareComposite->fitImage();
        else activeViewer()->fitImage();
        return;
    case CommandId::ZoomActualSize:
        if (isComparisonActive() && m_compare->layout() != playback::CompareLayout::SideBySide
            && m_compare->layout() != playback::CompareLayout::Stacked) m_compareComposite->showActualSize();
        else activeViewer()->showActualSize();
        return;
    case CommandId::ZoomIn:
        if (isComparisonActive() && m_compare->layout() != playback::CompareLayout::SideBySide
            && m_compare->layout() != playback::CompareLayout::Stacked) m_compareComposite->zoomIn();
        else activeViewer()->zoomIn();
        return;
    case CommandId::ZoomOut:
        if (isComparisonActive() && m_compare->layout() != playback::CompareLayout::SideBySide
            && m_compare->layout() != playback::CompareLayout::Stacked) m_compareComposite->zoomOut();
        else activeViewer()->zoomOut();
        return;
    case CommandId::FlipHorizontal:
        setFlipHorizontal(checked);
        return;
    case CommandId::MediaInformation:
        openMediaInformation();
        return;
    case CommandId::ToggleFullScreen:
        if (checked) {
            showFullScreen();
        } else {
            showNormal();
        }
        return;
    case CommandId::ToggleVideoFullScreen:
        if (checked) enterVideoFullScreen();
        else exitVideoFullScreen();
        return;
    case CommandId::ToggleComparison:
        if (checked) enterComparison(); else exitComparison();
        return;
    case CommandId::CompareSideBySide:
        if (checked) m_compare->setLayout(playback::CompareLayout::SideBySide);
        return;
    case CommandId::CompareStacked:
        if (checked) m_compare->setLayout(playback::CompareLayout::Stacked);
        return;
    case CommandId::CompareWipe:
        if (checked) m_compare->setLayout(playback::CompareLayout::Wipe);
        return;
    case CommandId::CompareBlend:
        if (checked) m_compare->setLayout(playback::CompareLayout::Blend);
        return;
    case CommandId::CompareDifference:
        if (checked) m_compare->setLayout(playback::CompareLayout::Difference);
        return;
    case CommandId::CompareBOffsetBackOneFrame:
        setSourceBOffsetFrames(framesForOffsetUs(m_compare->sourceBOffsetUs()) - 1); return;
    case CommandId::CompareBOffsetForwardOneFrame:
        setSourceBOffsetFrames(framesForOffsetUs(m_compare->sourceBOffsetUs()) + 1); return;
    case CommandId::ResetCompareBOffset:
        setSourceBOffsetFrames(0); return;
    case CommandId::ExternalAudioOffsetBackOneFrame:
        setExternalAudioOffsetFrames(framesForOffsetUs(m_compare->externalAudioOffsetUs()) - 1); return;
    case CommandId::ExternalAudioOffsetForwardOneFrame:
        setExternalAudioOffsetFrames(framesForOffsetUs(m_compare->externalAudioOffsetUs()) + 1); return;
    case CommandId::ResetExternalAudioOffset:
        setExternalAudioOffsetFrames(0); return;
    case CommandId::LoadExternalAudio:
        loadExternalAudio(); return;
    case CommandId::ClearExternalAudio:
        clearExternalAudio(); return;
    case CommandId::ToggleSourcesPanel:
        m_sourcesDock->setVisible(checked);
        return;
    case CommandId::ToggleBookmarksPanel:
        m_bookmarksDock->setVisible(checked);
        return;

    // --- Application ------------------------------------------------------
    case CommandId::Quit:
        close();
        return;
    case CommandId::Preferences:
        openPreferences();
        return;
    case CommandId::ShowWelcome:
        showWelcome();
        return;
    case CommandId::About:
        QMessageBox::about(
            this,
            tr("About ATK Player"),
            tr("<h3>%1 %2</h3>"
               "<p><b>Animation Tool Kit - Media Player</b></p>"
               "<p>This is the companion app for the Animation Tool Kit - Maya tools series.</p>"
               "<p><b>Created By David Shepstone</b></p>"
               "<p>This is an independent open-source development build.</p>")
                .arg(QString::fromLatin1(version::kApplicationName),
                     QString::fromLatin1(version::kString)));
        return;

    // --- Declared, not yet implemented ------------------------------------
    case CommandId::ShowReviewLibrary:
        showReviewLibrary();
        return;
    case CommandId::NewReview:
        newReviewDialog();
        return;
    case CommandId::RenameFromRoster:
        renameClipsFromRoster();
        return;
    case CommandId::OpenMedia:
        openMediaDialog();
        return;
    case CommandId::NewProject:
        newProject(); return;
    case CommandId::AddMediaToPlaylist:
        addMediaDialog(); return;
    case CommandId::CloseSource:
        m_playback->closeMedia();
        return;
    case CommandId::ToggleAudioScrub:
        m_playback->setAudioScrubEnabled(checked);
        m_settings->setAudioScrubEnabled(checked);
        statusBar()->showMessage(
            checked ? tr("Audio scrubbing on") : tr("Audio scrubbing off"), 1500);
        return;
    case CommandId::ToggleFrameStepAudio:
        m_playback->setFrameStepAudioEnabled(checked);
        m_settings->setFrameStepAudioEnabled(checked);
        statusBar()->showMessage(checked ? tr("Frame-step audio on") : tr("Frame-step audio off"), 1500);
        return;

    case CommandId::ToggleMute: {
        m_playback->setMuted(checked);
        m_settings->setMuted(checked);
        m_transport->setMuted(checked);
        statusBar()->showMessage(checked ? tr("Audio muted") : tr("Audio unmuted"), 1500);
        return;
    }
    case CommandId::VolumeUp:
        m_playback->setVolume(m_playback->volume() + 0.1);
        if (m_volumeSlider) m_volumeSlider->setValue(qRound(m_playback->volume() * 100));
        m_transport->setVolumePercent(qRound(m_playback->volume() * 100));
        m_settings->setVolume(m_playback->volume());
        statusBar()->showMessage(
            tr("Volume %1%").arg(qRound(m_playback->volume() * 100)), 1500);
        return;
    case CommandId::VolumeDown:
        m_playback->setVolume(m_playback->volume() - 0.1);
        if (m_volumeSlider) m_volumeSlider->setValue(qRound(m_playback->volume() * 100));
        m_transport->setVolumePercent(qRound(m_playback->volume() * 100));
        m_settings->setVolume(m_playback->volume());
        statusBar()->showMessage(
            tr("Volume %1%").arg(qRound(m_playback->volume() * 100)), 1500);
        return;
    case CommandId::TimelineZoomIn:
        m_timelineWidget->zoomIn();
        return;
    case CommandId::TimelineZoomOut:
        m_timelineWidget->zoomOut();
        return;
    case CommandId::TimelineZoomFit:
        m_timelineWidget->fitEntire();
        return;

    case CommandId::OpenProject: openProjectDialog(); return;
    case CommandId::SaveProject: saveProject(); return;
    case CommandId::SaveProjectAs: saveProjectAs(); return;
    case CommandId::ExportReview: exportReview(); return;
    case CommandId::ExportCurrentFrame: exportCurrentFrame(); return;
    case CommandId::ExportImageSequence: exportImageSequence(); return;
    case CommandId::ExportReviewNotes: exportReviewNotes(); return;
    case CommandId::ExportNotesVideo: exportReview(true); return;
    case CommandId::ExportReviewNotesBatch: exportReviewNotesBatch(); return;
    }
}

void MainWindow::reportNotImplemented(CommandId id)
{
    const commands::CommandDefinition* definition = commands::find(id);
    const QString name = definition != nullptr
        ? QCoreApplication::translate("Command", definition->displayName)
        : tr("This command");

    qCInfo(log::ui).noquote() << "Command not implemented:"
                              << (definition != nullptr ? definition->key : "unknown");

    statusBar()->showMessage(tr("%1 is not implemented yet.").arg(name), 3000);
}

void MainWindow::activateBookmark(quint64 id)
{
    const timeline::Bookmark* bookmark = m_timeline->bookmark(id);
    if (!bookmark) return;
    const timeline::Bookmark selected = *bookmark;
    m_bookmarks->selectBookmark(id);
    // A released timeline scrub can leave a temporary pointer-position overlay
    // until its exact target is presented. Bookmark navigation supersedes that
    // request; retaining the overlay would paint an abandoned frame even after
    // the controller/model/viewer reached the bookmark.
    m_timelineWidget->followAuthoritativeFrame();
    if (selected.isRange()) {
        m_playback->activateReviewRange(selected.frame, selected.endFrame);
    } else {
        m_timeline->ensureFrameVisible(selected.frame);
        m_playback->seekFrame(selected.frame);
    }
}

void MainWindow::openPreferences()
{
    PreferencesDialog dialog(*m_settings, *m_commands, this);
    dialog.setApiRuntimeStatus(m_apiServer->isRunning()
        ? tr("Listening on 127.0.0.1:%1").arg(m_apiServer->port())
        : (m_apiServer->errorString().isEmpty() ? tr("Stopped")
                                                : tr("Stopped — %1").arg(m_apiServer->errorString())));
    if (dialog.exec() == QDialog::Accepted) applyPreferences(dialog);
}

void MainWindow::openMediaInformation()
{
    if (!m_mediaInformationDialog) {
        m_mediaInformationDialog = new MediaInformationDialog(this);
        refreshMediaInformation();
    }
    m_mediaInformationDialog->show();
    m_mediaInformationDialog->raise();
    m_mediaInformationDialog->activateWindow();
}

void MainWindow::refreshMediaInformation()
{
    if (!m_mediaInformationDialog) return;
    if (m_playback->hasMedia()) {
        m_mediaInformationDialog->setMediaInformation(
            m_playback->metadata(), m_timeline->frameCount());
    } else {
        m_mediaInformationDialog->clearMediaInformation();
    }
}

void MainWindow::applyPreferences(const PreferencesDialog& dialog)
{
    const bool apiWasEnabled = m_settings->apiEnabled();
    const int apiWasPort = m_settings->apiPort();
    if (dialog.resetAllRequested()) {
        m_settings->resetAll();
        m_skipLayoutSaveOnce = true;
        m_playback->setVolume(ApplicationSettings::defaultVolume());
        m_playback->setMuted(ApplicationSettings::defaultMuted());
        m_settings->setMuted(ApplicationSettings::defaultMuted());
        m_transport->setMuted(false);
        if (m_volumeSlider) m_volumeSlider->setValue(100);
        if (QAction* mute = m_commands->action(CommandId::ToggleMute)) mute->setChecked(false);
    }
    m_settings->setRestoreWindowLayout(dialog.restoreWindowLayout());
    m_settings->setAudioScrubEnabled(dialog.audioScrubEnabled());
    m_settings->setFrameStepAudioEnabled(dialog.frameStepAudioEnabled());
    m_settings->setBookmarkSnapEnabled(dialog.bookmarkSnapEnabled());
    m_settings->setReopenLastProject(dialog.reopenLastProject());
    m_settings->setShowLibraryAtStartup(dialog.showLibraryAtStartup());
    // Applies to stills added from now on; existing sources keep their hold.
    m_settings->setStillImageHoldFrames(dialog.stillImageHoldFrames());
    m_settings->setImageSequenceFrameRate(dialog.imageSequenceFrameRate());
    const bool apiChanged = dialog.apiEnabled() != apiWasEnabled
        || dialog.apiPort() != apiWasPort;
    m_settings->setApiEnabled(dialog.apiEnabled());
    m_settings->setApiPort(dialog.apiPort());
    if (apiChanged) {
        m_apiServer->stop();
        if (dialog.apiEnabled()) {
            if (!m_apiServer->start(static_cast<quint16>(dialog.apiPort())))
                statusBar()->showMessage(
                    tr("Unable to start Local API: %1").arg(m_apiServer->errorString()), 8000);
        }
    }

    const auto setToggle = [this](CommandId id, bool checked) {
        if (QAction* action = m_commands->action(id)) {
            QSignalBlocker blocker(action);
            action->setChecked(checked);
        }
    };
    setToggle(CommandId::ToggleAudioScrub, dialog.audioScrubEnabled());
    setToggle(CommandId::ToggleFrameStepAudio, dialog.frameStepAudioEnabled());
    setToggle(CommandId::ToggleBookmarkSnap, dialog.bookmarkSnapEnabled());
    m_playback->setAudioScrubEnabled(dialog.audioScrubEnabled());
    m_playback->setFrameStepAudioEnabled(dialog.frameStepAudioEnabled());
    m_timelineWidget->setBookmarkSnapEnabled(dialog.bookmarkSnapEnabled());

    for (const commands::CommandDefinition& definition : commands::allCommands()) {
        const QString key = QString::fromLatin1(definition.key);
        const QString effective = dialog.shortcuts().value(key);
        const QString defaultValue = definition.defaultShortcut
            ? QKeySequence(QString::fromLatin1(definition.defaultShortcut)).toString(QKeySequence::PortableText)
            : QString();
        m_commands->setShortcut(definition.id,
            QKeySequence::fromString(effective, QKeySequence::PortableText));
        if (effective == defaultValue) m_settings->resetShortcutOverride(key);
        else m_settings->setShortcutOverride(key, effective);
    }
    m_settings->sync();
}

void MainWindow::restoreApplicationLayout()
{
    if (!m_settings->restoreWindowLayout()) return;
    const QByteArray geometry = m_settings->windowGeometry();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    bool visible = false;
    for (QScreen* screen : QGuiApplication::screens())
        visible = visible || screen->availableGeometry().intersects(frameGeometry());
    if (!visible) {
        resize(1280, 800);
        if (QScreen* screen = QGuiApplication::primaryScreen())
            move(screen->availableGeometry().center() - rect().center());
    }
    // The review header always has the top row to itself, above the drawing
    // tools. A saved layout that puts it anywhere else (one from before the
    // header existed) is not restored; the built-in arrangement stays.
    //
    // That is decided before restoring, never fixed up after: a window
    // restored while hidden and about to be maximized keeps a copy of the
    // restored layout that Qt applies once it is shown, and moving a toolbar
    // (or restoring a second state) deletes layout items that copy still
    // points to. 0.5.3 moved the header here and crashed on every start after
    // the window was closed maximized.
    const QByteArray state = m_settings->windowState();
    const bool restorable = !state.isEmpty()
        && (!m_reviewHeaderToolbar || !m_annotationToolbar
            || savedStateKeepsHeaderOnTop(state, m_reviewHeaderToolbar->objectName(),
                                          m_annotationToolbar->objectName()));
    if (restorable && !restoreState(state)) {
        addDockWidget(Qt::LeftDockWidgetArea, m_sourcesDock);
        addDockWidget(Qt::RightDockWidgetArea, m_bookmarksDock);
    }
    // The saved layout may record the drawing toolbar as visible; it belongs
    // to Draw on Frame, which always starts off.
    if (m_annotationToolbar) m_annotationToolbar->setVisible(m_viewer->annotateMode());
    // Notes always start closed, whatever the last session left open.
    if (m_notesDock) m_notesDock->hide();
    if (m_reviewHeaderToolbar) m_reviewHeaderToolbar->show();
}

void MainWindow::saveApplicationLayout()
{
    // Save the player's layout, not the library's (which hides the docks).
    showPlayer();
    if (!m_settings) return;
    if (!m_skipLayoutSaveOnce) {
        m_settings->setWindowGeometry(saveGeometry());
        m_settings->setWindowState(saveState());
    }
    m_settings->sync();
}

void MainWindow::onPlayerStateChanged(playback::PlayerState state)
{
    using playback::PlayerState;

    m_transport->setPlaying(state == PlayerState::Playing);
    updateTransportEnabled();

    switch (state) {
    case PlayerState::Playing:
        // A released scrub preview is presentation-only. Starting playback
        // always hands the playhead back to Source A's authoritative model,
        // including when the release landed on the already-current frame and
        // therefore produced no currentFrameChanged signal to clear it.
        m_timelineWidget->followAuthoritativeFrame();
        m_playlistPlaybackActive = true;
        statusBar()->showMessage(tr("Playing"), 1500);
        break;
    case PlayerState::Paused:
        m_playlistPlaybackActive = false;
        statusBar()->showMessage(tr("Paused"), 1500);
        break;
    case PlayerState::Ended:
        if (isComparisonActive()) {
            m_playlistPlaybackActive = false;
            statusBar()->showMessage(tr("End of comparison"), 2000);
        } else if (m_playlistPlaybackActive && (m_playlistMode == PlaylistMode::PlayThrough
                                                || m_playlistMode == PlaylistMode::LoopPlaylist)) {
            const bool wrap = m_playlistMode == PlaylistMode::LoopPlaylist;
            const int next = nextUsablePlaylistIndex(wrap);
            if (next >= 0 && next != m_project->activeIndex()) {
                activatePlaylistIndex(next, true);
                break;
            }
            if (next >= 0) {
                // Looping a playlist of one clip: play it again. Queued, as the
                // player is still reporting the end.
                QMetaObject::invokeMethod(this, [this] {
                    m_playback->goToStart();
                    m_playback->play();
                }, Qt::QueuedConnection);
                break;
            }
            m_playlistPlaybackActive = false;
            statusBar()->showMessage(tr("End of playlist"), 2000);
        } else if (m_playlistMode == PlaylistMode::StopAfterClip) {
            m_playlistPlaybackActive = false;
            statusBar()->showMessage(tr("End of clip (Space plays the next)"), 3000);
        } else {
            m_playlistPlaybackActive = false;
            statusBar()->showMessage(tr("End of playlist"), 2000);
        }
        break;
    case PlayerState::Loading:
        m_viewer->setLoading();
        statusBar()->showMessage(tr("Loading media..."));
        break;
    case PlayerState::Error:
        m_viewer->setError(m_playback->errorMessage());
        break;
    case PlayerState::Empty:
        m_viewer->setEmpty();
        break;
    case PlayerState::Ready:
    case PlayerState::Seeking:
        break;
    }
}

void MainWindow::updateTransportEnabled()
{
    // Transport is meaningful whenever there is an extent to move along --
    // real media, or the labelled placeholder before anything is opened.
    const bool hasExtent = m_timeline->frameCount() > 0;
    const bool notErrored = m_playback->state() != playback::PlayerState::Error;
    const bool enabled = hasExtent && notErrored;
    m_frameNumberInput->setMediaAvailable(m_playback->hasMedia() && notErrored);

    if (QAction* action = m_commands->action(CommandId::ExportReview))
        action->setEnabled(m_playback->hasMedia() && !exportInProgress());
    for (CommandId id : {CommandId::ExportCurrentFrame, CommandId::ExportImageSequence, CommandId::ExportNotesVideo})
        if (QAction* action = m_commands->action(id))
            action->setEnabled(m_playback->hasMedia() && !exportInProgress());
    if (QAction* action = m_commands->action(CommandId::ExportReviewNotes))
        action->setEnabled(m_playback->hasMedia() && !notesPdfInProgress());
    if (QAction* action = m_commands->action(CommandId::ExportReviewNotesBatch))
        action->setEnabled(!m_project->entries().isEmpty() && !notesPdfInProgress());

    if (QAction* action = m_commands->action(CommandId::ToggleVideoFullScreen)) {
        action->setEnabled(m_playback->hasMedia());
    }
    int usableSources = 0;
    for (int i = 0; i < m_project->entries().size(); ++i)
        if (sourceUsableForComparison(i)) ++usableSources;
    m_commands->action(CommandId::ToggleComparison)->setEnabled(m_playback->hasMedia() && usableSources >= 2);
    m_commands->action(CommandId::CompareSideBySide)->setEnabled(isComparisonActive());
    m_commands->action(CommandId::CompareStacked)->setEnabled(isComparisonActive());
    for (CommandId id : {CommandId::CompareWipe, CommandId::CompareBlend,
                         CommandId::CompareDifference, CommandId::CompareBOffsetBackOneFrame,
                         CommandId::CompareBOffsetForwardOneFrame, CommandId::ResetCompareBOffset,
                         CommandId::ExternalAudioOffsetBackOneFrame,
                         CommandId::ExternalAudioOffsetForwardOneFrame,
                         CommandId::ResetExternalAudioOffset})
        m_commands->action(id)->setEnabled(isComparisonActive());
    m_commands->action(CommandId::LoadExternalAudio)->setEnabled(isComparisonActive());
    m_commands->action(CommandId::ClearExternalAudio)->setEnabled(
        isComparisonActive() && !m_compare->externalAudioPath().isEmpty());

    for (const commands::CommandId id : { commands::CommandId::PlayPause,
                                          commands::CommandId::Stop,
                                          commands::CommandId::PreviousFrame,
                                          commands::CommandId::NextFrame,
                                          commands::CommandId::FirstFrame,
                                          commands::CommandId::LastFrame,
                                          commands::CommandId::ToggleLoop }) {
        if (QAction* action = m_commands->action(id)) {
            action->setEnabled(enabled);
        }
    }

    const bool audio = m_playback->hasAudioOutput();
    for (const commands::CommandId id : { commands::CommandId::ToggleMute,
                                          commands::CommandId::VolumeUp,
                                          commands::CommandId::VolumeDown }) {
        if (QAction* action = m_commands->action(id)) {
            action->setEnabled(audio);
        }
    }
}

void MainWindow::installPlaceholderTimeline()
{
    m_timeline->setPlaceholderExtent(kPlaceholderFrameCount,
                                     media::FrameRate::fromInteger(kPlaceholderFps));
}

void MainWindow::buildAudioControls()
{
    QMenu* audioMenu = nullptr;
    const QList<QAction*> menuActions = menuBar()->actions();
    for (QAction* action : menuActions) {
        if (action->menu() != nullptr
            && action->text() == commands::categoryTitle(commands::CommandCategory::Audio)) {
            audioMenu = action->menu();
            break;
        }
    }
    if (audioMenu == nullptr) {
        return;
    }

    audioMenu->addSeparator();

    // A slider embedded in the menu is enough for M1; a mixer is not the point.
    auto* container = new QWidget(audioMenu);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(12, 4, 12, 4);

    auto* label = new QLabel(tr("Volume"), container);
    label->setProperty("atkRole", "statusCaption");
    layout->addWidget(label);

    m_volumeSlider = new QSlider(Qt::Horizontal, container);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(qRound(m_playback->volume() * 100));
    m_volumeSlider->setAccessibleName(tr("Volume level"));
    m_volumeSlider->setFixedWidth(140);
    layout->addWidget(m_volumeSlider);

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_playback->setVolume(value / 100.0);
        m_settings->setVolume(value / 100.0);
        m_transport->setVolumePercent(value);
    });

    auto* action = new QWidgetAction(audioMenu);
    action->setDefaultWidget(container);
    audioMenu->addAction(action);
}

void MainWindow::openMediaDialog()
{
    // FFmpeg decides what is readable, so "All Files" is offered alongside the
    // common filters rather than the extension list being the gate.
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Media"), m_lastMediaDirectory, mediaFileFilter());

    if (path.isEmpty()) {
        return;
    }

    m_lastMediaDirectory = QFileInfo(path).absolutePath();
    openMediaFile(path);
}

void MainWindow::openMediaFile(const QString& filePath)
{
    if (!cancelExportForProjectChange()) return;
    exitComparison();
    if (!confirmDiscardChanges()) return;
    ++m_projectGeneration;
    m_restoringSourceState = true;
    m_project->clear();
    m_project->setName(QStringLiteral("Untitled"));
    m_project->setFilePath(QString());
    m_restoringSourceState = false;
    addMediaFiles({filePath});
}

void MainWindow::setFlipHorizontal(bool flipped)
{
    m_flipHorizontal = flipped;
    m_viewer->setFlipHorizontal(flipped);
    if (m_viewerB) m_viewerB->setFlipHorizontal(flipped);
    if (m_compareComposite) m_compareComposite->setFlipHorizontal(flipped);
    if (QAction* action = m_commands->action(CommandId::FlipHorizontal)) {
        const QSignalBlocker blocker(action);
        action->setChecked(flipped);
    }
}

void MainWindow::onMediaOpened(const media::MediaMetadata& metadata)
{
    // Like zoom and pan, a mirror belongs to the picture being looked at; a
    // newly opened source starts unflipped so it is never misread.
    setFlipHorizontal(false);
    m_viewer->resetNavigationToFit();
    m_viewer->setSourceAspectRatio(
        metadata.resolution.height() > 0
            ? (static_cast<double>(metadata.resolution.width()) * metadata.pixelAspectRatio)
                  / static_cast<double>(metadata.resolution.height())
            : 0.0);

    // The waveform is indexed by media time and the track by frame, so the
    // widget needs the duration to map between them.
    m_timelineWidget->setWaveform(&m_playback->waveform());
    m_timelineWidget->setMediaDuration(metadata.durationUs);

    if (m_project->activeIndex() >= 0) {
        auto& entry = m_project->mutableEntries()[m_project->activeIndex()];
        entry.missing = false;
        entry.source->setMetadata(metadata);
        restoreActiveReviewState();
    } else {
        m_sources->setCurrentMedia(metadata.fileName, metadata.shortDescription());
    }
    m_statusInfo->setMediaInfo(metadata.fileName, metadata.hasExactFrameCount());

    if (isComparisonActive() && m_compare->audioMode() != playback::CompareAudioMode::SourceA)
        applyComparisonAudioMode(m_compare->audioMode());

    updateTransportEnabled();
    updateWindowTitle();
    refreshMediaInformation();

    statusBar()->showMessage(tr("Opened %1").arg(metadata.fileName), 3000);
    if (m_playAfterSourceOpen) {
        m_playAfterSourceOpen = false;
        m_playback->play();
    }
}

int MainWindow::nextUsablePlaylistIndex(bool wrap) const
{
    const int count = int(m_project->entries().size());
    const int active = m_project->activeIndex();
    const int steps = wrap ? count : count - active - 1;
    for (int step = 1; step <= steps; ++step) {
        const int i = (active + step) % count;
        const auto availability = m_project->entries().at(i).availability;
        // Playing through follows the playlist's category filter.
        if (availability != project::SourceAvailability::Missing
            && availability != project::SourceAvailability::Error
            && m_sources->showsCategory(m_project->entries().at(i).category)) {
            return i;
        }
    }
    return -1;
}

void MainWindow::setPlaylistMode(PlaylistMode mode)
{
    m_playlistMode = mode;
    // Each side of the loop button remembers its choice.
    if (mode == PlaylistMode::PlayThrough || mode == PlaylistMode::StopAfterClip) {
        m_stopAfterEachClip = mode == PlaylistMode::StopAfterClip;
        m_settings->setStopAfterEachClip(m_stopAfterEachClip);
    } else {
        m_loopWholePlaylist = mode == PlaylistMode::LoopPlaylist;
        m_settings->setLoopWholePlaylist(m_loopWholePlaylist);
    }
    // The player loops a clip by itself; a playlist loops here, clip to clip.
    const bool loop = mode == PlaylistMode::LoopClip || mode == PlaylistMode::LoopPlaylist;
    if (m_playback->isLoopEnabled() != (mode == PlaylistMode::LoopClip))
        m_playback->setLoopEnabled(mode == PlaylistMode::LoopClip);
    const auto check = [this](CommandId id, bool on) {
        if (QAction* action = m_commands->action(id)) {
            QSignalBlocker blocker(action);
            action->setChecked(on);
        }
    };
    check(CommandId::ToggleLoop, loop);
    check(CommandId::PlaylistPlayThrough, mode == PlaylistMode::PlayThrough);
    check(CommandId::PlaylistStopAfterClip, mode == PlaylistMode::StopAfterClip);
    check(CommandId::PlaylistLoopClip, mode == PlaylistMode::LoopClip);
    check(CommandId::PlaylistLoopAll, mode == PlaylistMode::LoopPlaylist);
}

void MainWindow::onMediaError(const QString& message)
{
    m_restoringSourceState = false;
    m_sources->clearCurrentMedia();
    m_statusInfo->clearMediaInfo();
    updateTransportEnabled();
    updateWindowTitle();
    statusBar()->showMessage(message, 8000);
}

void MainWindow::addMediaDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Media to Playlist"), m_lastMediaDirectory, mediaFileFilter());
    if (!paths.isEmpty()) { m_lastMediaDirectory = QFileInfo(paths.first()).absolutePath(); addMediaFiles(paths); }
}

void MainWindow::addMediaFiles(const QStringList& paths, bool interactive)
{
    showPlayer(); // a dropped, opened or API-sent clip always lands in the player
    const bool wasEmpty = m_project->entries().isEmpty();
    // Preferences seed an image source's hold and rate once, here; the source
    // keeps them from then on, so a later preference change cannot move or
    // re-time saved bookmarks.
    media::ImageSourceOptions still;
    still.holdFrames = m_settings->stillImageHoldFrames();
    still.frameRate = m_settings->imageSequenceFrameRate();
    // One decision per sequence: selecting or dropping all 96 frames of a
    // render asks once, not 96 times.
    QHash<QString, ImageInterpretation> sequenceDecisions;
    for (const QString& path : paths) {
        if (path.isEmpty()) continue;
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (interactive && media::isStillImagePath(absolute)) {
            if (const auto sequence = media::detectImageSequence(absolute)) {
                const auto known = sequenceDecisions.constFind(sequence->pattern);
                if (known != sequenceDecisions.cend()) {
                    if (*known == ImageInterpretation::Sequence || *known == ImageInterpretation::Skip)
                        continue; // already added, or declined
                } else {
                    const ImageInterpretation choice = askImageInterpretation(*sequence, absolute);
                    sequenceDecisions.insert(sequence->pattern, choice);
                    if (choice == ImageInterpretation::Skip) continue;
                    if (choice == ImageInterpretation::Sequence) {
                        media::ImageSourceOptions options = still;
                        options.sequenceFirst = sequence->first;
                        options.sequenceLast = sequence->last;
                        auto source = std::make_shared<media::MediaSource>(sequence->pattern);
                        source->setImageOptions(options);
                        m_project->addSource(std::move(source));
                        continue;
                    }
                }
            }
        }
        auto source = std::make_shared<media::MediaSource>(absolute);
        if (source->isStillImage()) source->setImageOptions(still);
        m_project->addSource(std::move(source));
    }
    if (wasEmpty && !m_project->entries().isEmpty()) {
        m_project->setActiveIndex(-1);
        activatePlaylistIndex(0);
    }
    startPlaylistProbes();
    updateWindowTitle();
}

MainWindow::ImageInterpretation MainWindow::askImageInterpretation(
    const media::ImageSequence& sequence, const QString& path)
{
    // Asking, rather than guessing, because a numbered file is just as often a
    // single reference still as one frame of a render.
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("ImageSequencePrompt"));
    box.setWindowTitle(tr("Image Sequence"));
    box.setIcon(QMessageBox::Question);
    QString text = tr("%1 is part of a numbered image sequence:\n%2 — %3 frames.")
                       .arg(QFileInfo(path).fileName(),
                            media::sequenceDisplayName(sequence.pattern, sequence.first, sequence.last),
                            QString::number(sequence.length()));
    if (sequence.missingCount() > 0)
        text += QLatin1Char('\n') + tr("%n frame(s) missing; they will hold the previous frame.",
                                       nullptr, static_cast<int>(sequence.missingCount()));
    box.setText(text);
    box.setInformativeText(tr("Open the whole sequence at %1, or just this image?")
                               .arg(media::frameRateLabel(m_settings->imageSequenceFrameRate())));
    QPushButton* asSequence = box.addButton(tr("Image Sequence"), QMessageBox::AcceptRole);
    asSequence->setObjectName(QStringLiteral("ImageSequencePromptSequence"));
    QPushButton* asStill = box.addButton(tr("Single Image"), QMessageBox::ActionRole);
    asStill->setObjectName(QStringLiteral("ImageSequencePromptStill"));
    QPushButton* skip = box.addButton(QMessageBox::Cancel);
    skip->setObjectName(QStringLiteral("ImageSequencePromptSkip"));
    box.setDefaultButton(asSequence);
    box.exec();
    if (box.clickedButton() == asSequence) return ImageInterpretation::Sequence;
    if (box.clickedButton() == asStill) return ImageInterpretation::SingleStill;
    return ImageInterpretation::Skip;
}

bool MainWindow::setSourceFrameRate(int index, const media::FrameRate& rate)
{
    if (index < 0 || index >= m_project->entries().size() || !rate.isValid()) return false;
    auto& entry = m_project->mutableEntries()[index];
    if (!entry.source || !(entry.source->isImageSequence() || entry.source->isStillImage())) return false;
    media::ImageSourceOptions options = entry.source->imageOptions();
    if (options.frameRate == rate) return true;
    const bool active = index == m_project->activeIndex();
    if (active) saveActiveReviewState();
    options.frameRate = rate;
    entry.source->setImageOptions(options);
    m_project->setModified(true);
    startProbe(entry.id, entry.source->filePath());
    // Frame indices are unchanged, so bookmarks and the range stay put; the
    // reopen re-derives their media times from the new rate.
    if (active) {
        m_project->setActiveIndex(-1);
        activatePlaylistIndex(index, false);
    } else if (isComparisonActive() && entry.id == m_compare->sourceBId()) {
        openComparisonSourceB();
    }
    return true;
}

void MainWindow::changeSourceFrameRate(int index)
{
    if (index < 0 || index >= m_project->entries().size()) return;
    const auto& source = m_project->entries().at(index).source;
    if (!source || !(source->isImageSequence() || source->isStillImage())) return;
    QStringList labels;
    for (const media::FrameRate& rate : media::imageFrameRatePresets())
        labels << media::frameRateLabel(rate);
    const int current = static_cast<int>(
        media::imageFrameRatePresets().indexOf(source->imageOptions().frameRate));
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Frame Rate"), tr("Playback rate for %1:").arg(source->displayName()),
        labels, current >= 0 ? current : 1, false, &ok);
    if (!ok) return;
    const int chosenIndex = static_cast<int>(labels.indexOf(chosen));
    if (chosenIndex >= 0) setSourceFrameRate(index, media::imageFrameRatePresets().at(chosenIndex));
}

void MainWindow::startProbe(const QUuid& id, const QString& path)
{
    const quint64 token = m_nextProbeToken++;
    if (!m_project->beginProbe(id, path, token)) return;
    const int index = m_project->indexForId(id);
    const media::ImageSourceOptions still = m_project->entries().at(index).source->imageOptions();
    QMetaObject::invokeMethod(m_probeWorker, [worker = m_probeWorker, id, path, token, still] {
        worker->probe(id, path, token, still);
    }, Qt::QueuedConnection);
}

void MainWindow::refreshReviewHeader()
{
    if (!m_reviewHeader) return;
    QString projectName = m_project->name().trimmed();
    if (projectName.isEmpty() && !m_project->filePath().isEmpty())
        projectName = QFileInfo(m_project->filePath()).completeBaseName();
    if (projectName.isEmpty()) projectName = tr("Untitled project");
    const int index = m_project->activeIndex();
    if (index < 0 || index >= m_project->entries().size() || !m_project->entries().at(index).source) {
        m_reviewHeader->setLocation(projectName, QString(), false);
        m_reviewHeader->setStatus(project::ClipStatus::NotReviewed);
        return;
    }
    const project::SourceEntry& entry = m_project->entries().at(index);
    m_reviewHeader->setLocation(projectName,
                                entry.displayName.isEmpty() ? entry.source->displayName() : entry.displayName, true);
    m_reviewHeader->setStatus(entry.status);
}

QString MainWindow::thumbnailCacheDirectory() const
{
    // Beside the settings file when one was given (tests), else the user cache.
    return !m_settingsIniPath.isEmpty()
        ? QFileInfo(m_settingsIniPath).absoluteDir().filePath(QStringLiteral("thumbnails"))
        : QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("thumbnails"));
}

void MainWindow::requestPreviewThumbnail(const QString& clipPath)
{
    // Library strips show clips by path; a name-based id keeps one request per clip.
    static const QUuid kNamespace(QStringLiteral("{7d3c1f3e-5a8b-4b8e-9c1d-2f6a4e0b9a11}"));
    const QUuid id = QUuid::createUuidV5(kNamespace, clipPath);
    if (m_previewThumbnailRequests.contains(id) || !m_thumbnailWorker || !QFileInfo(clipPath).isFile()) return;
    m_previewThumbnailRequests.insert(id, clipPath);
    const QString cacheDirectory = thumbnailCacheDirectory();
    const quint64 token = m_nextThumbnailToken++;
    QMetaObject::invokeMethod(m_thumbnailWorker, [worker = m_thumbnailWorker, id, clipPath, token, cacheDirectory] {
        worker->render(id, clipPath, token, {}, QSize(320, 180), cacheDirectory);
    }, Qt::QueuedConnection);
}

void MainWindow::showStartScreen()
{
    if (m_settings->showLibraryAtStartup()) showReviewLibrary();
}

bool MainWindow::isShowingReviewLibrary() const
{
    return m_libraryPage && m_pages->currentWidget() == m_libraryPage;
}

void MainWindow::showReviewLibrary()
{
    if (!m_libraryPage) {
        m_libraryPage = new ReviewLibraryPage(m_reviewLibrary.get(), m_pages);
        m_pages->addWidget(m_libraryPage);
        connect(m_libraryPage, &ReviewLibraryPage::newReviewRequested, this, &MainWindow::newReviewDialog);
        connect(m_libraryPage, &ReviewLibraryPage::openReviewRequested, this, [this](const QUuid& id) { openReview(id); });
        connect(m_libraryPage, &ReviewLibraryPage::openProjectRequested, this, &MainWindow::openProjectDialog);
        connect(m_libraryPage, &ReviewLibraryPage::addExistingRequested, this, &MainWindow::addExistingReviewDialog);
        connect(m_libraryPage, &ReviewLibraryPage::openMediaRequested, this, &MainWindow::openMediaDialog);
        connect(m_libraryPage, &ReviewLibraryPage::showFolderRequested, this,
                [](const QString& folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
        connect(m_libraryPage, &ReviewLibraryPage::libraryChanged, this, &MainWindow::saveReviewLibrary);
        connect(m_libraryPage, &ReviewLibraryPage::thumbnailWanted, this, &MainWindow::requestPreviewThumbnail);
    }
    if (isShowingReviewLibrary()) {
        m_libraryPage->refresh();
        return;
    }
    // Leave the player in a quiet state: no drawing mode, nothing playing,
    // not full screen. The project stays open; the player is one click away.
    if (m_viewer->annotateMode()) setAnnotateMode(false);
    exitVideoFullScreen();
    if (m_playback->isPlaying()) m_playback->pause();
    m_hiddenForLibrary.clear();
    for (QWidget* widget : {static_cast<QWidget*>(m_sourcesDock), static_cast<QWidget*>(m_bookmarksDock),
                            static_cast<QWidget*>(m_notesDock), static_cast<QWidget*>(m_reviewHeaderToolbar)}) {
        if (widget && widget->isVisible()) {
            m_hiddenForLibrary.append(widget);
            widget->hide();
        }
    }
    m_statusInfo->setReadoutVisible(false);
    m_viewerZoomStatus->hide();
    statusBar()->clearMessage();
    statusBar()->hide();
    m_pages->setCurrentWidget(m_libraryPage);
    m_libraryPage->refresh();
}

void MainWindow::showPlayer()
{
    if (!isShowingReviewLibrary()) return;
    m_pages->setCurrentWidget(m_playerPage);
    for (QWidget* widget : std::as_const(m_hiddenForLibrary)) widget->show();
    m_hiddenForLibrary.clear();
    m_statusInfo->setReadoutVisible(true);
    m_viewerZoomStatus->show();
    statusBar()->show();
}

bool MainWindow::saveReviewLibrary()
{
    if (QString error; !m_reviewLibrary->save(&error)) {
        statusBar()->showMessage(tr("The review library could not be saved: %1").arg(error), 6000);
        return false;
    }
    return true;
}

void MainWindow::syncReviewLibraryEntry()
{
    const review::ReviewInfo* found = m_reviewLibrary->findByProject(m_project->filePath());
    if (!found) return;
    review::ReviewInfo info = *found;
    info.clipCount = int(m_project->entries().size());
    info.previewClips.clear();
    for (const project::SourceEntry& entry : m_project->entries()) {
        if (info.previewClips.size() >= 5) break;
        if (entry.source && !entry.source->isImageSequence()) info.previewClips.append(entry.source->filePath());
    }
    if (info != *found) {
        m_reviewLibrary->update(info);
        saveReviewLibrary();
        if (m_libraryPage) m_libraryPage->refresh();
    }
}

void MainWindow::offerRenameRecovery(const QString& folder)
{
    for (const QString& journal : review::interruptedJournals(folder)) {
        const auto answer = QMessageBox::question(
            this, tr("Unfinished Rename"),
            tr("A rename of clips in this folder did not finish (the computer may have shut down part way).\n\n"
               "Put the original file names back?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) continue;
        const review::RenameResult result = review::recoverInterrupted(journal);
        if (result.ok)
            statusBar()->showMessage(tr("Restored %n file name(s).", nullptr, result.renamed), 6000);
        else
            QMessageBox::warning(this, tr("Unfinished Rename"), result.error);
    }
}

void MainWindow::retargetRenamedClips(const QHash<QString, QString>& moved)
{
    auto key = [](const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); };
    QHash<QString, QString> byKey;
    for (auto it = moved.cbegin(); it != moved.cend(); ++it) byKey.insert(key(it.key()), it.value());
    QVector<QPair<QUuid, QString>> renamed;
    for (project::SourceEntry& entry : m_project->mutableEntries()) {
        if (!entry.source) continue;
        const auto it = byKey.constFind(key(entry.source->filePath()));
        if (it == byKey.cend()) continue;
        // Same clip, same metadata, new file name: drawings, notes,
        // bookmarks and status stay with it.
        auto replacement = std::make_shared<media::MediaSource>(*it);
        replacement->setMetadata(entry.source->metadata());
        replacement->setImageOptions(entry.source->imageOptions());
        entry.source = std::move(replacement);
        entry.missing = false;
        renamed.append({entry.id, QFileInfo(*it).fileName()});
    }
    for (const auto& [id, name] : renamed) m_project->setDisplayName(id, name);
    m_project->setModified(true);
}

void MainWindow::renameClipsFromRoster()
{
    if (m_project->entries().isEmpty()) {
        QMessageBox::information(this, tr("Rename Clips from Team List"),
                                 tr("Open a review, or add clips to the playlist, first."));
        return;
    }
    showPlayer();
    QVector<BatchRenameDialog::Clip> clips;
    for (const project::SourceEntry& entry : m_project->entries()) {
        if (!entry.source) continue;
        clips.append({entry.id, entry.source->filePath(),
                      entry.displayName.isEmpty() ? entry.source->displayName() : entry.displayName,
                      entry.source->isImageSequence()});
    }
    offerRenameRecovery(QFileInfo(clips.first().path).absolutePath());
    BatchRenameDialog dialog(clips, m_project->name(), m_settings->renameTemplate(), this);
    if (const QString last = m_settings->lastRosterPath(); !last.isEmpty() && QFileInfo::exists(last))
        dialog.loadRoster(last);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!dialog.undoJournal().isEmpty()) {
        QString error;
        if (undoRosterRename(dialog.undoJournal(), &error))
            statusBar()->showMessage(tr("The last rename was undone."), 6000);
        else
            QMessageBox::warning(this, tr("Undo Rename"), error);
        return;
    }
    if (!dialog.rosterPath().isEmpty()) m_settings->setLastRosterPath(dialog.rosterPath());
    m_settings->setRenameTemplate(dialog.nameTemplate());
    QString error;
    if (!applyRosterRename(dialog.decisions(), dialog.renameOnDisk(), dialog.orderByRoster(), &error))
        QMessageBox::warning(this, tr("Rename Clips"), tr("Nothing was renamed.\n\n%1").arg(error));
    else
        statusBar()->showMessage(dialog.renameOnDisk() ? tr("Files renamed. Undo is in the same window.")
                                                       : tr("Clips renamed in ATK Player. The files are unchanged."),
                                 6000);
}

bool MainWindow::applyRosterRename(const QVector<review::RenameDecision>& decisions, bool onDisk, bool reorder,
                                   QString* error)
{
    const QUuid active = m_project->currentSourceId();
    auto reactivate = [this, active] {
        const int index = m_project->indexForId(active);
        if (index >= 0 && m_project->activeIndex() != index) {
            m_project->setActiveIndex(-1);
            activatePlaylistIndex(index);
        }
    };
    if (onDisk) {
        QVector<review::RenameItem> items;
        for (const review::RenameDecision& d : decisions) {
            const int index = m_project->indexForId(d.id);
            if (d.newPath.isEmpty() || index < 0 || !m_project->entries().at(index).source) continue;
            items.append({m_project->entries().at(index).source->filePath(), d.newPath});
        }
        const review::RenamePlan plan = review::planRenames(items);
        if (!plan.ok()) {
            if (error) *error = tr("Some new names cannot be used; nothing was renamed.");
            return false;
        }
        if (plan.changeCount() > 0) {
            // The player holds the open clip; release it so Windows lets it be
            // renamed (the rename retries while the player's threads let go).
            m_playback->releaseMediaFiles();
            m_project->setActiveIndex(-1);
            const review::RenameResult result = review::executeRenames(plan);
            if (!result.ok) {
                if (error) *error = result.error;
                reactivate();
                return false;
            }
            QHash<QString, QString> moved;
            for (const review::PlannedRename& r : plan.items)
                if (r.problem == review::RenameProblem::None) moved.insert(r.from, r.to);
            retargetRenamedClips(moved);
        }
    }
    // The names shown in ATK Player (and in the notes PDF and exports).
    for (const review::RenameDecision& d : decisions) {
        const int index = m_project->indexForId(d.id);
        if (d.newName.isEmpty() || index < 0 || !m_project->entries().at(index).source) continue;
        const auto& source = m_project->entries().at(index).source;
        const QString suffix = source->isImageSequence() ? QString() : QFileInfo(source->filePath()).suffix();
        m_project->setDisplayName(d.id, suffix.isEmpty() ? d.newName : d.newName + QLatin1Char('.') + suffix);
    }
    if (reorder)
        for (int target = 0; target < decisions.size(); ++target) {
            const int from = m_project->indexForId(decisions[target].id);
            if (from >= 0 && from != target) m_project->moveSource(from, target);
        }
    // After renaming files the saved project must point at the new names.
    if (onDisk && !m_project->filePath().isEmpty()) saveProjectTo(m_project->filePath());
    reactivate();
    updateWindowTitle();
    return true;
}

bool MainWindow::undoRosterRename(const QString& journalPath, QString* error)
{
    QVector<review::RenameItem> items;
    QString state;
    if (!review::readRenameJournal(journalPath, &items, &state)) {
        if (error) *error = tr("The rename record could not be read.");
        return false;
    }
    const QUuid active = m_project->currentSourceId();
    m_playback->releaseMediaFiles();
    m_project->setActiveIndex(-1);
    const review::RenameResult result = review::undoRenames(journalPath);
    if (result.ok) {
        QHash<QString, QString> moved;
        for (const review::RenameItem& item : items) moved.insert(item.to, item.from);
        retargetRenamedClips(moved);
        if (!m_project->filePath().isEmpty()) saveProjectTo(m_project->filePath());
    } else if (error) {
        *error = result.error;
    }
    if (const int index = m_project->indexForId(active); index >= 0) activatePlaylistIndex(index);
    updateWindowTitle();
    return result.ok;
}

void MainWindow::newReviewDialog()
{
    NewReviewDialog dialog(m_lastMediaDirectory, this);
    if (dialog.exec() != QDialog::Accepted) return;
    createReview(dialog.reviewName(), dialog.folder(), dialog.includeSubfolders(), dialog.chosenClips(),
                 dialog.projectPath());
}

namespace {
/// A clip found in a review folder as a media source with the user's image
/// defaults (and a sequence's frame range).
std::shared_ptr<media::MediaSource> sourceForClip(const review::FoundClip& clip, media::ImageSourceOptions still)
{
    auto source = std::make_shared<media::MediaSource>(clip.path);
    if (clip.sequence) {
        still.sequenceFirst = clip.sequenceFirst;
        still.sequenceLast = clip.sequenceLast;
        source->setImageOptions(still);
    } else if (source->isStillImage()) {
        source->setImageOptions(still);
    }
    return source;
}
} // namespace

bool MainWindow::createReview(const QString& name, const QString& folder, bool includeSubfolders,
                              const QVector<review::FoundClip>& clips, const QString& projectPath, bool interactive)
{
    if (clips.isEmpty() || projectPath.isEmpty()) return false;
    if (!cancelExportForProjectChange()) return false;
    exitComparison();
    if (interactive && !confirmDiscardChanges()) return false;
    ++m_projectGeneration;
    m_playback->closeMedia();
    m_project->replace(name, QString(), {}, QUuid{});
    media::ImageSourceOptions still;
    still.holdFrames = m_settings->stillImageHoldFrames();
    still.frameRate = m_settings->imageSequenceFrameRate();
    for (const review::FoundClip& clip : clips) m_project->addSource(sourceForClip(clip, still));

    review::ReviewInfo info;
    info.name = name;
    info.folder = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    info.projectPath = QFileInfo(projectPath).absoluteFilePath();
    info.includeSubfolders = includeSubfolders;
    info.createdMsUtc = info.openedMsUtc = QDateTime::currentMSecsSinceEpoch();
    m_reviewLibrary->add(info); // before saving, so the save fills in its count and strip
    if (!saveProjectTo(projectPath, false, interactive)) {
        if (const review::ReviewInfo* added = m_reviewLibrary->findByProject(projectPath))
            m_reviewLibrary->remove(added->id);
        return false;
    }
    m_project->setName(name);
    saveReviewLibrary();
    m_lastMediaDirectory = info.folder;
    showPlayer();
    m_project->setActiveIndex(-1);
    activatePlaylistIndex(0);
    startPlaylistProbes();
    updateWindowTitle();
    return true;
}

bool MainWindow::openReview(const QUuid& id, bool interactive)
{
    const review::ReviewInfo* found = m_reviewLibrary->find(id);
    if (!found) return false;
    const review::ReviewInfo info = *found;
    if (!QFileInfo::exists(info.projectPath)) {
        if (interactive
            && QMessageBox::question(this, tr("Open Review"),
                   tr("The project file for \"%1\" is missing:\n%2\n\nRemove the review from the library?")
                       .arg(info.name, QDir::toNativeSeparators(info.projectPath)),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            m_reviewLibrary->remove(id);
            saveReviewLibrary();
            if (m_libraryPage) m_libraryPage->refresh();
        }
        return false;
    }
    if (interactive && !confirmDiscardChanges()) return false;
    if (interactive) offerRenameRecovery(info.folder); // before the files are opened
    if (!openProjectFile(info.projectPath)) return false;

    review::ReviewInfo opened = info;
    opened.openedMsUtc = QDateTime::currentMSecsSinceEpoch();
    m_reviewLibrary->update(opened);

    // Clips added to the folder since: offer them, at the end.
    if (interactive && QFileInfo(info.folder).isDir()) {
        QSet<QString> known;
        for (const project::SourceEntry& entry : m_project->entries())
            if (entry.source) known.insert(QDir::cleanPath(entry.source->filePath()).toCaseFolded());
        QVector<review::FoundClip> added;
        for (const review::FoundClip& clip : review::scanFolder(info.folder, info.includeSubfolders))
            if (!known.contains(QDir::cleanPath(clip.path).toCaseFolded())) added.append(clip);
        if (!added.isEmpty()) {
            QStringList names;
            for (const review::FoundClip& clip : added.mid(0, 8)) names << clip.displayName;
            if (added.size() > 8) names << tr("…and %n more", nullptr, int(added.size() - 8));
            const auto answer = QMessageBox::question(
                this, tr("New Clips"),
                tr("%n new clip(s) in the review folder:", nullptr, int(added.size())) + QStringLiteral("\n\n")
                    + names.join(QLatin1Char('\n')) + QStringLiteral("\n\n") + tr("Add them to the end of the review?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer == QMessageBox::Yes) {
                media::ImageSourceOptions still;
                still.holdFrames = m_settings->stillImageHoldFrames();
                still.frameRate = m_settings->imageSequenceFrameRate();
                for (const review::FoundClip& clip : added) m_project->addSource(sourceForClip(clip, still));
                startPlaylistProbes();
                saveProjectTo(m_project->filePath());
            }
        }
    }
    syncReviewLibraryEntry();
    saveReviewLibrary();
    return true;
}

void MainWindow::requestThumbnail(const QUuid& sourceId)
{
    const int index = m_project->indexForId(sourceId);
    if (index < 0 || !m_thumbnailWorker) return;
    const auto& source = m_project->entries().at(index).source;
    if (!source) return;
    const QString cacheDirectory = thumbnailCacheDirectory();
    const QString path = source->filePath();
    const media::ImageSourceOptions still = source->imageOptions();
    const quint64 token = m_nextThumbnailToken++;
    QMetaObject::invokeMethod(m_thumbnailWorker, [worker = m_thumbnailWorker, sourceId, path, token, still, cacheDirectory] {
        worker->render(sourceId, path, token, still, QSize(320, 180), cacheDirectory);
    }, Qt::QueuedConnection);
}

void MainWindow::startPlaylistProbes()
{
    for (const auto& entry : m_project->entries())
        if (entry.source) startProbe(entry.id, entry.source->filePath());
}

void MainWindow::saveActiveReviewState()
{
    if (m_project->activeIndex() < 0 || m_restoringSourceState) return;
    auto& entry = m_project->mutableEntries()[m_project->activeIndex()];
    entry.bookmarks = m_timeline->bookmarks();
    entry.playbackRange = m_timeline->playbackRange();
}

void MainWindow::syncInOutButtons()
{
    if (!m_setInButton || !m_setOutButton || !m_inOutToggle) return;
    const bool marked = m_timeline->hasPlaybackMarks();
    const bool kept = m_timeline->hasKeptMarks();
    const timeline::PlaybackRange& range = m_timeline->playbackRange();
    auto shortcut = [this](CommandId id) {
        const QAction* action = m_commands->action(id);
        const QString keys = action ? action->shortcut().toString(QKeySequence::NativeText) : QString();
        return keys.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(keys);
    };
    const QString clearHint = tr("Right-click to clear the in and out points.");
    m_setInButton->setToolTip(
        (marked ? tr("Set the in point to the current frame%1. In point: frame %2.")
                      .arg(shortcut(CommandId::SetRangeIn)).arg(range.startFrame + 1)
                : tr("Set the in point to the current frame%1. Playback starts here.")
                      .arg(shortcut(CommandId::SetRangeIn)))
        + (marked ? QLatin1Char('\n') + clearHint : QString()));
    m_setOutButton->setToolTip(
        (marked ? tr("Set the out point to the current frame%1. Out point: frame %2.")
                      .arg(shortcut(CommandId::SetRangeOut)).arg(range.endFrame + 1)
                : tr("Set the out point to the current frame%1. Playback stops or loops here.")
                      .arg(shortcut(CommandId::SetRangeOut)))
        + (marked ? QLatin1Char('\n') + clearHint : QString()));
    for (QToolButton* button : {m_setInButton, m_setOutButton}) {
        if (button->property("markSet").toBool() == marked) continue;
        button->setProperty("markSet", marked);
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    if (QAction* clear = m_commands->action(CommandId::ClearRange)) clear->setEnabled(marked || kept);

    const QString toggleKeys = shortcut(CommandId::ToggleInOut);
    m_inOutToggle->setChecked(marked);
    m_inOutToggle->setText(marked ? tr("On") : tr("Off"));
    m_inOutToggle->setToolTip(
        marked ? tr("In and out points on (frames %1 to %2). Click to turn them off and play the whole clip%3.")
                     .arg(range.startFrame + 1).arg(range.endFrame + 1).arg(toggleKeys)
        : kept ? tr("In and out points off: the whole clip plays. Click to turn them back on (frames %1 to %2)%3.")
                     .arg(range.startFrame + 1).arg(range.endFrame + 1).arg(toggleKeys)
               : tr("Turns the in and out points on and off%1. Set them first with [ and ].").arg(toggleKeys));
    if (QAction* toggle = m_commands->action(CommandId::ToggleInOut)) {
        const QSignalBlocker blocker(toggle);
        toggle->setChecked(marked);
        toggle->setEnabled(marked || kept);
    }
    m_inOutToggle->setEnabled(marked || kept);
}

void MainWindow::restoreActiveReviewState()
{
    if (m_project->activeIndex() < 0) return;
    const auto& entry = m_project->entries().at(m_project->activeIndex());
    m_restoringSourceState = true;
    m_timeline->clearBookmarks();
    for (const auto& bookmark : entry.bookmarks) m_timeline->addBookmark(bookmark);
    // The whole clip on screen, with this clip's in/out marks (reviews saved
    // before the marks kept them as the visible range, which reads the same).
    m_timeline->fitViewport();
    m_timeline->setPlaybackRange(entry.playbackRange.isValid() ? entry.playbackRange : timeline::PlaybackRange{});
    if (m_timeline->hasPlaybackMarks()) m_playback->seekFrame(m_timeline->effectiveStartFrame());
    m_viewer->resetNavigationToFit();
    m_restoringSourceState = false;
    bindAnnotationsToActiveSource();
}

void MainWindow::activatePlaylistIndex(int index, bool continuePlayback)
{
    if (index < 0 || index >= m_project->entries().size() || index == m_project->activeIndex()) return;
    saveActiveReviewState();
    const auto& entry = m_project->entries().at(index);
    if (!entry.source || entry.availability == project::SourceAvailability::Missing
        || entry.availability == project::SourceAvailability::Error) {
        statusBar()->showMessage(tr("Missing media: %1").arg(entry.displayName), 5000);
        if (continuePlayback && index + 1 < m_project->entries().size()) activatePlaylistIndex(index + 1, true);
        return;
    }
    m_project->setActiveIndex(index);
    m_playAfterSourceOpen = continuePlayback;
    m_restoringSourceState = true;
    m_playback->openMedia(entry.source->filePath(), entry.source->imageOptions());
}

void MainWindow::removePlaylistIndex(int index)
{
    if (index < 0 || index >= m_project->entries().size()) return;
    const bool current = index == m_project->activeIndex();
    if (current) saveActiveReviewState();
    const int next = index + 1 < m_project->entries().size() ? index : index - 1;
    m_project->removeSourceAt(index);
    if (current) {
        if (next >= 0 && next < m_project->entries().size()) activatePlaylistIndex(next);
        else m_playback->closeMedia();
    }
}

void MainWindow::movePlaylistIndex(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_project->entries().size() || to >= m_project->entries().size()) return;
    m_project->moveSource(from, to);
}

void MainWindow::arrangePlaylist()
{
    if (m_project->entries().isEmpty()) {
        statusBar()->showMessage(tr("Add clips to the playlist first."), 3000);
        return;
    }
    QVector<ArrangePlaylistDialog::Clip> clips;
    for (const project::SourceEntry& entry : m_project->entries()) {
        ArrangePlaylistDialog::Clip clip;
        clip.id = entry.id;
        const QString path = entry.source ? entry.source->filePath() : entry.storedPath;
        clip.name = !entry.displayName.isEmpty() ? entry.displayName
                  : entry.source                 ? entry.source->displayName()
                                                 : QFileInfo(path).fileName();
        clip.modified = QFileInfo(path).lastModified();
        if (entry.source && entry.availability == project::SourceAvailability::Ready)
            clip.durationUs = entry.source->metadata().durationUs;
        clip.category = entry.category;
        clip.status = entry.status;
        clips.append(clip);
    }
    ArrangePlaylistDialog dialog(std::move(clips), m_project->categories(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    applyPlaylistArrangement(dialog.clips(), dialog.categories());
}

void MainWindow::applyPlaylistArrangement(const QVector<ArrangePlaylistDialog::Clip>& clips,
                                          const QStringList& categories)
{
    m_project->setCategories(categories);
    QVector<QUuid> order;
    for (const ArrangePlaylistDialog::Clip& clip : clips) {
        m_project->setClipCategory(clip.id, clip.category);
        order.append(clip.id);
    }
    m_project->reorder(order);
    updateWindowTitle();
    statusBar()->showMessage(tr("Playlist arranged."), 3000);
}

bool MainWindow::confirmDiscardChanges()
{
    if (!m_project->needsSavePrompt()) return true;
    const auto choice = QMessageBox::warning(this, tr("Unsaved Project"),
        tr("Save changes to the current project?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) return saveProject();
    return true;
}

void MainWindow::newProject()
{
    if (!cancelExportForProjectChange()) return;
    exitComparison();
    if (!confirmDiscardChanges()) return;
    ++m_projectGeneration;
    m_playback->closeMedia();
    m_project->replace(QStringLiteral("Untitled"), QString(), {}, QUuid{});
}

void MainWindow::openProjectDialog()
{
    if (!confirmDiscardChanges()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(),
                                                      project::ProjectSerializer::fileDialogFilter());
    if (!path.isEmpty()) openProjectFile(path);
}

bool MainWindow::openProjectFile(const QString& path)
{
    if (!cancelExportForProjectChange()) return false;
    exitComparison();
    project::Project loaded;
    media::ImageSourceOptions defaultStill;
    defaultStill.holdFrames = m_settings->stillImageHoldFrames();
    defaultStill.frameRate = m_settings->imageSequenceFrameRate();
    const auto result = project::ProjectSerializer::load(loaded, path, defaultStill);
    if (!result.ok) {
        if (!m_suppressProjectOpenError) QMessageBox::critical(this, tr("Open Project"), result.errorMessage);
        return false;
    }
    ++m_projectGeneration;
    m_playback->closeMedia();
    m_project->replace(loaded.name(), loaded.filePath(), loaded.entries(), loaded.currentSourceId(),
                       loaded.exportFolder(), loaded.exportBurnIns(), loaded.categories());
    int current = m_project->activeIndex();
    if (current >= 0 && m_project->entries().at(current).missing) {
        int usable = -1;
        for (int i = current + 1; i < m_project->entries().size(); ++i) if (!m_project->entries().at(i).missing) { usable = i; break; }
        if (usable < 0) for (int i = current - 1; i >= 0; --i) if (!m_project->entries().at(i).missing) { usable = i; break; }
        current = usable;
    }
    if (current >= 0 && !m_project->entries().at(current).missing) {
        m_project->setActiveIndex(-1);
        activatePlaylistIndex(current);
    } else {
        m_project->setActiveIndex(-1);
    }
    m_settings->addRecentProject(path); refreshRecentProjectsMenu();
    addOpenedProjectToReviewLibrary();
    startPlaylistProbes();
    showPlayer();
    updateWindowTitle(); return true;
}

namespace {

/// A library entry for a project that was not made with New Review.
review::ReviewInfo reviewInfoForProject(const project::Project& project, const QString& projectPath)
{
    review::ReviewInfo info;
    // A project never named says "Untitled"; its file name says more.
    const QString name = project.name();
    info.name = name.isEmpty() || name == QLatin1StringView("Untitled") ? QFileInfo(projectPath).completeBaseName()
                                                                        : name;
    // The clips' folder when they share one, for Show Folder and for offering
    // clips added there later; otherwise the project's own folder.
    QSet<QString> clipFolders;
    for (const project::SourceEntry& entry : project.entries()) {
        if (!entry.source) continue;
        clipFolders.insert(QDir::cleanPath(QFileInfo(entry.source->filePath()).absolutePath()));
        if (info.previewClips.size() < 5 && !entry.source->isImageSequence())
            info.previewClips.append(entry.source->filePath());
    }
    info.folder = clipFolders.size() == 1 ? *clipFolders.cbegin()
                                          : QDir::cleanPath(QFileInfo(projectPath).absolutePath());
    info.projectPath = QFileInfo(projectPath).absoluteFilePath();
    info.clipCount = int(project.entries().size());
    info.createdMsUtc = info.openedMsUtc = QDateTime::currentMSecsSinceEpoch();
    return info;
}

} // namespace

void MainWindow::addOpenedProjectToReviewLibrary()
{
    // A project opened directly (Open Project, Recent Projects, Explorer) joins
    // the Review Library too, so it is there next time. New Review and Open
    // Review already list theirs; they only get their counts brought up to date.
    if (m_project->filePath().isEmpty()) return;
    if (const review::ReviewInfo* listed = m_reviewLibrary->findByProject(m_project->filePath())) {
        // Already listed: it was just opened, which the Latest order and the
        // row's date go by.
        review::ReviewInfo opened = *listed;
        opened.openedMsUtc = QDateTime::currentMSecsSinceEpoch();
        m_reviewLibrary->update(opened);
    } else {
        m_reviewLibrary->add(reviewInfoForProject(*m_project, m_project->filePath()));
    }
    saveReviewLibrary();
    syncReviewLibraryEntry();
}

int MainWindow::addExistingReviews(const QStringList& paths, QStringList* failed)
{
    int added = 0;
    media::ImageSourceOptions defaultStill;
    defaultStill.holdFrames = m_settings->stillImageHoldFrames();
    defaultStill.frameRate = m_settings->imageSequenceFrameRate();
    for (const QString& path : paths) {
        // Read, not opened: the player keeps what it has.
        project::Project loaded;
        const auto result = project::ProjectSerializer::load(loaded, path, defaultStill);
        if (!result.ok) {
            if (failed) failed->append(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), result.errorMessage));
            continue;
        }
        review::ReviewInfo info = reviewInfoForProject(loaded, path);
        if (const review::ReviewInfo* existing = m_reviewLibrary->findByProject(path)) {
            // Already listed: bring it back from the archive and its counts up to date.
            const review::ReviewInfo old = *existing;
            info.id = old.id;
            info.name = old.name;
            info.starred = old.starred;
            info.includeSubfolders = old.includeSubfolders;
            info.folder = old.folder;
            info.createdMsUtc = old.createdMsUtc;
            info.openedMsUtc = old.openedMsUtc;
            m_reviewLibrary->update(info);
        } else {
            m_reviewLibrary->add(info);
        }
        ++added;
    }
    if (added > 0) {
        if (m_libraryPage) m_libraryPage->refresh();
        // Not written, they would be gone next time: leave the save error showing.
        if (!saveReviewLibrary()) {
            if (failed) failed->append(tr("The review library could not be saved, so the reviews will not be listed "
                                          "next time."));
            return 0;
        }
        statusBar()->showMessage(tr("Added %n review(s) to the library.", nullptr, added), 6000);
    }
    return added;
}

void MainWindow::addExistingReviewDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add Existing Reviews"), m_lastMediaDirectory,
                                                            project::ProjectSerializer::fileDialogFilter());
    if (paths.isEmpty()) return;
    QStringList failed;
    addExistingReviews(paths, &failed);
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Add Existing Reviews"),
                             tr("Not added:\n\n%1").arg(failed.join(QLatin1Char('\n'))));
}

bool MainWindow::saveProjectTo(const QString& path, bool saveAs, bool showError)
{
    saveActiveReviewState();
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const QString fileName = QFileInfo(absolutePath).fileName();
    // Say a save is under way, and then that it landed: a large project or a
    // synced folder can take a moment, and silence leaves the user guessing.
    statusBar()->showMessage(tr("Saving %1…").arg(fileName));
    statusBar()->repaint();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = saveAs
        ? project::ProjectSerializer::saveAs(*m_project, absolutePath)
        : project::ProjectSerializer::save(*m_project, absolutePath);
    QApplication::restoreOverrideCursor();
    m_lastProjectSaveError = result.errorMessage;
    if (!result.ok) {
        statusBar()->showMessage(tr("%1 was not saved.").arg(fileName), 8000);
        if (showError) QMessageBox::critical(this, tr("Save Project"), result.errorMessage);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1 at %2")
                                 .arg(fileName, QLocale().toString(QTime::currentTime(), QLocale::ShortFormat)),
                             6000);
    m_project->setFilePath(absolutePath);
    if (saveAs) m_project->setName(QFileInfo(absolutePath).completeBaseName());
    m_project->setModified(false);
    m_settings->addRecentProject(absolutePath); refreshRecentProjectsMenu();
    syncReviewLibraryEntry();
    updateWindowTitle(); return true;
}

void MainWindow::refreshRecentProjectsMenu()
{
    if (!m_recentProjectsMenu) return;
    m_recentProjectsMenu->clear();
    const QStringList recent = m_settings->recentProjects();
    for (const QString& path : recent) {
        QAction* action = m_recentProjectsMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        action->setEnabled(QFileInfo::exists(path));
        connect(action, &QAction::triggered, this, [this, path] {
            if (confirmDiscardChanges()) openProjectFile(path);
        });
    }
    if (!recent.isEmpty()) m_recentProjectsMenu->addSeparator();
    QAction* clear = m_recentProjectsMenu->addAction(tr("Clear Recent Projects"));
    clear->setEnabled(!recent.isEmpty());
    connect(clear, &QAction::triggered, this, [this] { m_settings->clearRecentProjects(); refreshRecentProjectsMenu(); });
}

bool MainWindow::reopenLastProjectIfEnabled()
{
    if (!m_settings->reopenLastProject()) return false;
    const QString path = m_settings->lastProjectPath();
    m_suppressProjectOpenError = true;
    const bool opened = !path.isEmpty() && QFileInfo::exists(path) && openProjectFile(path);
    m_suppressProjectOpenError = false;
    if (!opened) {
        m_settings->setLastProjectPath(QString());
        statusBar()->showMessage(tr("The last project could not be reopened."), 5000);
    }
    return opened;
}

void MainWindow::relinkSelectedMedia()
{
    const int index = m_sources->selectedIndex();
    if (index < 0 || index >= m_project->entries().size()) return;
    const auto id = m_project->entries().at(index).id;
    const QString path = QFileDialog::getOpenFileName(this, tr("Relink Media"), QString(),
        mediaFileFilter());
    const QFileInfo info(path);
    if (path.isEmpty()) return;
    if (!info.exists() || !info.isFile()) { QMessageBox::critical(this, tr("Relink Media"), tr("The selected media file does not exist.")); return; }
    m_pendingRelinkId = id;
    m_pendingRelinkPath = info.absoluteFilePath();
    m_pendingRelinkToken = m_nextProbeToken++;
    m_pendingRelinkProjectGeneration = m_projectGeneration;
    // Validate with the hold the replacement will get, so the validated extent
    // is the one it actually has: a still keeps its own hold (its bookmarks
    // were made against it); anything else takes the default for new stills.
    // A sequence relinked to a frame of another sequence becomes that whole
    // sequence at its own rate.
    const auto& relinked = m_project->entries().at(index);
    media::ImageSourceOptions still;
    if (relinked.source && relinked.source->isStillImage()) {
        still = relinked.source->imageOptions();
    } else {
        still.holdFrames = m_settings->stillImageHoldFrames();
        still.frameRate = m_settings->imageSequenceFrameRate();
    }
    if (relinked.source && relinked.source->isImageSequence()) {
        if (const auto sequence = media::detectImageSequence(m_pendingRelinkPath)) {
            still.frameRate = relinked.source->imageOptions().frameRate;
            still.sequenceFirst = sequence->first;
            still.sequenceLast = sequence->last;
            m_pendingRelinkPath = sequence->pattern;
        }
    }
    m_pendingRelinkStill = still;
    QMetaObject::invokeMethod(m_probeWorker,
        [worker = m_probeWorker, id, path = m_pendingRelinkPath, token = m_pendingRelinkToken, still] {
            worker->probe(id, path, token, still);
        }, Qt::QueuedConnection);
    statusBar()->showMessage(tr("Validating replacement media..."));
}

bool MainWindow::saveProject()
{
    return m_project->filePath().isEmpty() ? saveProjectAs() : saveProjectTo(m_project->filePath());
}

bool MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"), m_project->filePath(),
                                                project::ProjectSerializer::fileDialogFilter());
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".atkproj"), Qt::CaseInsensitive)) path += QStringLiteral(".atkproj");
    return saveProjectTo(path, true);
}

exporter::ExportSpec MainWindow::exportSnapshot(const QString& outputPath) const
{
    exporter::ExportSpec spec;
    int aIndex = m_project->activeIndex();
    if (isComparisonActive()) aIndex = m_project->indexForId(m_compare->sourceAId());
    if (aIndex < 0 || aIndex >= m_project->entries().size()) return spec;

    const auto snapshotSource = [](const project::SourceEntry& entry, qint64 start, qint64 end) {
        exporter::ExportSource result;
        result.id = entry.id;
        result.path = entry.source ? entry.source->filePath() : entry.storedPath;
        result.metadata = entry.source ? entry.source->metadata() : media::MediaMetadata{};
        if (entry.source) result.still = entry.source->imageOptions();
        result.rangeStartFrame = start;
        result.rangeEndFrame = end;
        return result;
    };
    const auto& a = m_project->entries().at(aIndex);
    spec.sourceA = snapshotSource(a, m_timeline->effectiveStartFrame(), m_timeline->effectiveEndFrame());
    spec.bookmarks.reserve(m_timeline->bookmarks().size());
    for (const auto& bookmark : m_timeline->bookmarks()) {
        spec.bookmarks.push_back({bookmark.id, bookmark.isRange(), bookmark.frame,
            bookmark.isRange() ? bookmark.endFrame : bookmark.frame,
            bookmark.name, bookmark.note, bookmark.colorIndex});
    }
    spec.comparison = isComparisonActive();
    // Exports keep the viewer's tracing paper unless it is turned off there.
    if (!spec.comparison) spec.burnIns.tracingPaperPercent = m_tracingPaper;
    // Drawings and notes of Source A. On by default when there are any, so
    // "Export Review" gives the SyncSketch-style annotated video in one step.
    if (!spec.comparison && a.id == m_annotationSourceId) {
        spec.annotations = m_annotations->annotations();
        if (!spec.annotations.isEmpty()) {
            spec.burnIns.annotations = true;
            spec.burnIns.annotationNotes = true;
        }
    }
    if (spec.comparison) {
        const int bIndex = m_project->indexForId(m_compare->sourceBId());
        if (bIndex >= 0) {
            const auto& b = m_project->entries().at(bIndex);
            const qint64 last = std::max<qint64>(0, b.source->metadata().effectiveFrameCount() - 1);
            spec.sourceB = snapshotSource(b,
                b.playbackRange.enabled ? b.playbackRange.startFrame : 0,
                b.playbackRange.enabled ? b.playbackRange.endFrame : last);
        }
        spec.layout = m_compare->layout();
        spec.sourceBOffsetUs = m_compare->sourceBOffsetUs();
        spec.wipePosition = m_compare->wipePosition();
        spec.blendAmount = m_compare->blendAmount();
        spec.audioMode = m_compare->audioMode();
        spec.externalAudioPath = m_compare->externalAudioPath();
        spec.externalAudioOffsetUs = m_compare->externalAudioOffsetUs();
    }
    spec.videoEncoder = exporter::FFmpegExporter::availableH264Encoder();
    if (!outputPath.isEmpty()) {
        spec.outputPath = outputPath;
    } else {
        const QFileInfo aFile(spec.sourceA.path);
        // The project's save location when one is set (and still there),
        // otherwise beside the project, or beside the clip when untitled.
        const QString directory = projectExportFolderAvailable() ? m_project->exportFolder()
            : m_project->filePath().isEmpty() ? aFile.absolutePath()
                                              : QFileInfo(m_project->filePath()).absolutePath();
        QString name = exportBaseName(spec.sourceA);
        if (spec.comparison) name += QStringLiteral("_vs_") + exportBaseName(spec.sourceB);
        spec.outputPath = QDir(directory).filePath(name + QStringLiteral("_review.mp4"));
    }
    return spec;
}

void MainWindow::rememberExportBurnIns(const std::optional<exporter::ExportBurnIns>& burnIns)
{
    if (m_project->exportBurnIns() == burnIns) return;
    m_project->setExportBurnIns(burnIns);
    updateWindowTitle();
}

void MainWindow::applySavedStillBurnIns(exporter::ExportSpec& spec) const
{
    // Stills have no play-through, hold or freeze; the rest of the review's
    // export choices carry over.
    std::optional<exporter::ExportBurnIns> saved = m_project->exportBurnIns();
    if (!saved) return;
    saved->annotationHoldFrames = 0;
    saved->annotationFreezeFrames = 0;
    if (spec.comparison) saved->tracingPaperPercent = spec.burnIns.tracingPaperPercent;
    spec.burnIns = *saved;
}

bool MainWindow::projectExportFolderAvailable() const
{
    return !m_project->exportFolder().isEmpty() && QFileInfo(m_project->exportFolder()).isDir();
}

bool MainWindow::exportInProgress() const
{
    return m_exportJob && m_exportJob->isRunning();
}

void MainWindow::exportReview(bool withNotes)
{
    if (exportInProgress()) return;
    exporter::ExportSpec spec = exportSnapshot();
    if (withNotes && !spec.comparison) {
        spec.burnIns.annotations = true;
        spec.burnIns.annotationNotes = true;
        const QFileInfo out(spec.outputPath);
        spec.outputPath = QDir(out.absolutePath()).filePath(out.completeBaseName().replace(
            QStringLiteral("_review"), QStringLiteral("_notes")) + QStringLiteral(".mp4"));
    }
    ExportDialog dialog(spec, this);
    dialog.setOpenWhenDone(m_settings->openExportsWhenDone());
    dialog.setProjectFolder(m_project->exportFolder(), !m_project->filePath().isEmpty());
    // Start from the choices this review was last exported with. "With notes"
    // still means the drawings and notes go in.
    std::optional<exporter::ExportBurnIns> saved = m_project->exportBurnIns();
    if (saved) {
        if (spec.comparison) saved->tracingPaperPercent = spec.burnIns.tracingPaperPercent;
        if (withNotes && !spec.comparison) saved->annotations = saved->annotationNotes = true;
    }
    dialog.setSavedBurnIns(saved);
    connect(&dialog, &ExportDialog::projectFolderChanged, this, [this](const QString& folder) {
        m_project->setExportFolder(folder);
        updateWindowTitle();
        statusBar()->showMessage(folder.isEmpty()
            ? tr("Exports of this project go beside the project again.")
            : tr("Exports of this project now go to %1. Save the project to keep this.")
                  .arg(QDir::toNativeSeparators(folder)), 6000);
    });
    if (dialog.exec() != QDialog::Accepted) return;
    spec.outputPath = dialog.destination();
    spec.burnIns = dialog.burnIns();
    // "Use Defaults" is for this export only: the saved choices stay.
    if (!dialog.rememberBurnIns()) rememberExportBurnIns(std::nullopt);
    else if (!dialog.defaultsForThisExportOnly()) rememberExportBurnIns(spec.burnIns);
    m_settings->setOpenExportsWhenDone(dialog.openWhenDone());
    m_openExportWhenDone = dialog.openWhenDone();
    if (QFileInfo::exists(spec.outputPath)
        && QMessageBox::question(this, tr("Export Review"),
            tr("Replace the existing file?\n%1").arg(QDir::toNativeSeparators(spec.outputPath)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const QString validation = spec.validate();
    if (!validation.isEmpty()) { QMessageBox::critical(this, tr("Export Review"), validation); return; }
    startExport(std::move(spec));
}

void MainWindow::exportCurrentFrame()
{
    if (exportInProgress() || !m_playback->hasMedia()) return;
    exporter::ExportSpec spec = exportSnapshot();
    spec.kind = exporter::ExportKind::CurrentFrame;
    spec.firstFrame = m_timeline->currentFrame();
    spec.lastFrame = m_timeline->currentFrame();
    const QString base = safeImagePrefix(exportBaseName(spec.sourceA));
    const int digits = std::max(4, static_cast<int>(QString::number(std::max<qint64>(1,
        spec.sourceA.metadata.effectiveFrameCount())).size()));
    const QString suggested = QDir(QFileInfo(spec.outputPath).absolutePath()).filePath(
        QStringLiteral("%1_frame_%2.png").arg(base)
            .arg(m_timeline->currentFrame() + 1, digits, 10, QLatin1Char('0')));
    QString path = QFileDialog::getSaveFileName(this, tr("Export Current Frame"), suggested,
                                                tr("PNG Image (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) path += QStringLiteral(".png");
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, tr("Export Current Frame"),
            tr("Replace the existing image?\n%1").arg(QDir::toNativeSeparators(path)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    applySavedStillBurnIns(spec);
    if (!chooseBurnIns(this, tr("Export Current Frame"), QDir::toNativeSeparators(path), spec.burnIns)) return;
    spec.outputPath = QFileInfo(path).absoluteFilePath();
    const QString validation = spec.validate();
    if (!validation.isEmpty()) { QMessageBox::critical(this, tr("Export Current Frame"), validation); return; }
    startExport(std::move(spec));
}

void MainWindow::exportImageSequence()
{
    if (exportInProgress() || !m_playback->hasMedia()) return;
    exporter::ExportSpec spec = exportSnapshot();
    spec.kind = exporter::ExportKind::ImageSequence;
    spec.imagePrefix = safeImagePrefix(exportBaseName(spec.sourceA));
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("Choose Parent Folder for Image Sequence"), QFileInfo(spec.outputPath).absolutePath());
    if (parent.isEmpty()) return;
    spec.outputPath = QDir(parent).filePath(spec.imagePrefix + QStringLiteral("_frames"));
    applySavedStillBurnIns(spec);
    if (!chooseBurnIns(this, tr("Export Image Sequence"),
                       tr("%n frame(s) as PNG images into %1", nullptr, int(spec.frameCount()))
                           .arg(QDir::toNativeSeparators(spec.outputPath)),
                       spec.burnIns)) return;
    const QString validation = spec.validate();
    if (!validation.isEmpty()) { QMessageBox::critical(this, tr("Export Image Sequence"), validation); return; }
    startExport(std::move(spec));
}

void MainWindow::startExport(exporter::ExportSpec spec, bool showProgressUi)
{
    if (exportInProgress()) return;
    m_exportJob = std::make_unique<exporter::ExportJob>(std::move(spec), this);
    if (showProgressUi) {
        const exporter::ExportKind kind = m_exportJob->spec().kind;
        m_exportProgress = new ExportProgressDialog(
            kind == exporter::ExportKind::ReviewVideo ? QStringLiteral("MP4") : QStringLiteral("PNG"),
            kind == exporter::ExportKind::ReviewVideo ? tr("Exporting video")
                : kind == exporter::ExportKind::CurrentFrame ? tr("Exporting frame") : tr("Exporting image sequence"),
            QDir::toNativeSeparators(m_exportJob->spec().outputPath), this);
        m_exportProgress->setWindowModality(Qt::NonModal);
        connect(m_exportProgress, &ExportProgressDialog::canceled,
                m_exportJob.get(), &exporter::ExportJob::cancel);
        connect(m_exportJob.get(), &exporter::ExportJob::progress, m_exportProgress,
                [this](int percent, qint64, qint64) {
                    if (m_exportProgress) m_exportProgress->setValue(percent);
                });
        connect(m_exportJob.get(), &exporter::ExportJob::statusChanged, m_exportProgress,
                [this](const QString& text) {
                    if (m_exportProgress) m_exportProgress->setLabelText(text);
                });
    }
    connect(m_exportJob.get(), &exporter::ExportJob::progress, this,
            [this](int percent, qint64 frame, qint64 total) {
                m_apiExportProgress = percent;
                m_apiExportFrame = frame;
                m_apiExportTotal = total;
            });
    const auto closeProgress = [this] {
        if (m_exportProgress) { m_exportProgress->finish(); m_exportProgress->deleteLater(); m_exportProgress = nullptr; }
        updateTransportEnabled();
    };
    connect(m_exportJob.get(), &exporter::ExportJob::completed, this, [this, closeProgress](const QString& path) {
        m_apiExportState = QStringLiteral("completed");
        m_apiExportProgress = 100;
        const bool renamed = m_exportJob && QFileInfo(path) != QFileInfo(m_exportJob->spec().outputPath);
        closeProgress();
        statusBar()->showMessage(renamed
            ? tr("The old file was open in another app, so this export is saved as %1").arg(QDir::toNativeSeparators(path))
            : tr("Exported %1").arg(QDir::toNativeSeparators(path)), 10000);
        if (std::exchange(m_openExportWhenDone, false)) openExportedFile(path);
    });
    connect(m_exportJob.get(), &exporter::ExportJob::cancelled, this, [this, closeProgress] {
        m_apiExportState = QStringLiteral("cancelled");
        closeProgress(); statusBar()->showMessage(tr("Export cancelled"), 4000);
    });
    connect(m_exportJob.get(), &exporter::ExportJob::failed, this,
            [this, closeProgress, showProgressUi](const QString& message) {
        m_apiExportState = QStringLiteral("failed");
        m_apiExportError = message;
        closeProgress();
        if (showProgressUi) QMessageBox::critical(this, tr("Export Review"), message);
    });
    updateTransportEnabled();
    if (m_exportProgress) m_exportProgress->show();
    m_exportJob->start();
}

void MainWindow::openExportedFile(const QString& path)
{
    // A sync client (Google Drive, OneDrive, Dropbox) grabs a new file to
    // upload it, and a viewer started in that moment gets "Access denied".
    // Wait until the file is free (readable and held by no other program),
    // checking a few times a second for up to 30 s, then open it.
    constexpr int kIntervalMs = 300;
    constexpr int kMaxAttempts = 100;
    auto attempt = std::make_shared<std::function<void(int)>>();
    // The retry holds the function alive; the function itself only refers
    // to it weakly, so nothing outlives the last retry.
    *attempt = [this, path, weak = std::weak_ptr<std::function<void(int)>>(attempt)](int tries) {
        QFile probe(path);
        const bool readable = probe.open(QIODevice::ReadOnly) && probe.size() > 0;
        // Close our own handle first: on Windows the exclusivity check would
        // otherwise see it and report the file as busy forever.
        probe.close();
        const bool free = readable && !platform::PlatformInfo::isFileOpenElsewhere(path);
        if (!free && tries < kMaxAttempts) {
            if (tries == 0)
                statusBar()->showMessage(tr("Saved %1; opening it once it is free (a sync app may be uploading it)…")
                                             .arg(QDir::toNativeSeparators(path)));
            if (auto self = weak.lock())
                QTimer::singleShot(kIntervalMs, this, [self, tries] { (*self)(tries + 1); });
            return;
        }
        m_lastOpenedExport = path;
        if (qEnvironmentVariableIsSet("ATK_NO_OPEN_EXPORTS")) return; // tests
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            statusBar()->showMessage(tr("Exported %1, but no app could open it.").arg(QDir::toNativeSeparators(path)), 8000);
        else if (tries > 0)
            statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(path)), 6000);
    };
    (*attempt)(0);
}

bool MainWindow::notesPdfInProgress() const
{
    return (m_notesPdfJob && m_notesPdfJob->isRunning()) || m_notesBatch;
}

void MainWindow::exportReviewNotes()
{
    if (notesPdfInProgress() || !m_playback->hasMedia()) return;
    exporter::ExportSpec spec = exportSnapshot();
    // Notes belong to one source: the PDF always lists the annotated source
    // on its own, over the whole clip, whatever the comparison or range.
    spec.comparison = false;
    spec.sourceB = {};
    spec.burnIns = {};
    spec.annotations = spec.sourceA.id == m_annotationSourceId ? m_annotations->annotations()
                                                              : annotation::SourceAnnotations{};
    spec.sourceA.rangeStartFrame = 0;
    spec.sourceA.rangeEndFrame = std::max<qint64>(0, spec.sourceA.metadata.effectiveFrameCount() - 1);
    const QString suggested = QDir(QFileInfo(spec.outputPath).absolutePath())
        .filePath(exportBaseName(spec.sourceA) + QStringLiteral("_review_notes.pdf"));
    ReviewNotesDialog dialog(spec.annotations, suggested, m_tracingPaper, this);
    dialog.setOpenWhenDone(m_settings->openExportsWhenDone());
    dialog.setIncludeNoteText(m_settings->reviewNotesIncludeText());
    if (dialog.exec() != QDialog::Accepted) return;
    m_settings->setOpenExportsWhenDone(dialog.openWhenDone());
    m_settings->setReviewNotesIncludeText(dialog.includeNoteText());
    const bool openWhenDone = dialog.openWhenDone();
    const exporter::ReviewNotesOptions options = dialog.options();
    if (options.outputPath.isEmpty()) return;
    if (QFileInfo::exists(options.outputPath)
        && QMessageBox::question(this, tr("Export Review Notes"),
            tr("Replace the existing file?\n%1").arg(QDir::toNativeSeparators(options.outputPath)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

    // Header text, decided now on the UI thread.
    exporter::ReviewNotesDocument header = notesPdfHeader(spec, options.includeResolved);

    m_notesPdfJob = std::make_unique<exporter::ReviewNotesJob>(std::move(spec), std::move(header), options, this);
    m_notesPdfProgress = new ExportProgressDialog(QStringLiteral("PDF"), tr("Exporting notes PDF"),
                                                  QDir::toNativeSeparators(options.outputPath), this);
    m_notesPdfProgress->setLabelText(tr("Rendering the drawn frames…"));
    m_notesPdfProgress->setWindowModality(Qt::NonModal);
    connect(m_notesPdfProgress, &ExportProgressDialog::canceled, m_notesPdfJob.get(), &exporter::ReviewNotesJob::cancel);
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::progress, m_notesPdfProgress, [this](int percent) {
        if (!m_notesPdfProgress) return;
        m_notesPdfProgress->setValue(percent);
        if (percent >= 90) m_notesPdfProgress->setLabelText(tr("Writing PDF…"));
    });
    const auto finish = [this] {
        m_notesPdfJob->wait(); // the worker has reported; join it
        if (m_notesPdfProgress) {
            m_notesPdfProgress->finish();
            m_notesPdfProgress->deleteLater();
            m_notesPdfProgress = nullptr;
        }
        updateTransportEnabled();
    };
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::completed, this,
            [this, finish, openWhenDone, requested = options.outputPath](const QString& path, int pages) {
        finish();
        statusBar()->showMessage(QFileInfo(path) != QFileInfo(requested)
            ? tr("The old PDF was open in another app, so this one is saved as %1").arg(QDir::toNativeSeparators(path))
            : tr("Exported %1 (%n page(s))", nullptr, pages).arg(QDir::toNativeSeparators(path)), 10000);
        if (openWhenDone) openExportedFile(path);
    });
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::cancelled, this, [this, finish] {
        finish();
        statusBar()->showMessage(tr("Export cancelled"), 4000);
    });
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::failed, this, [this, finish](const QString& message) {
        finish();
        QMessageBox::critical(this, tr("Export Review Notes"), message);
    });
    m_notesPdfProgress->show();
    m_notesPdfJob->start();
    updateTransportEnabled();
}

exporter::ReviewNotesDocument MainWindow::notesPdfHeader(const exporter::ExportSpec& spec, bool includeResolved) const
{
    exporter::ReviewNotesDocument header;
    header.projectName = m_project->filePath().isEmpty() ? tr("Untitled project")
                                                         : QFileInfo(m_project->filePath()).completeBaseName();
    header.clipName = QFileInfo(spec.sourceA.path).fileName();
    QStringList reviewers;
    for (const annotation::FrameAnnotation& frame : spec.annotations.frames())
        for (const annotation::FrameNote& note : frame.notes)
            if ((includeResolved || !note.resolved) && !note.author.trimmed().isEmpty()
                && !reviewers.contains(note.author.trimmed()))
                reviewers.append(note.author.trimmed());
    header.reviewers = reviewers.join(QStringLiteral(", "));
    header.generatedText = tr("from %1").arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::LongFormat));
    header.footerLeft = QStringLiteral("ATK Player %1").arg(QString::fromLatin1(version::kString));
    return header;
}

exporter::ExportSpec MainWindow::notesPdfSpec(const project::SourceEntry& entry) const
{
    // As Export Notes PDF: the clip on its own, over the whole clip. The open
    // clip's notes come from the editor, the rest from the project.
    exporter::ExportSpec spec;
    spec.sourceA.id = entry.id;
    spec.sourceA.path = entry.source ? entry.source->filePath() : entry.storedPath;
    spec.sourceA.metadata = entry.source ? entry.source->metadata() : media::MediaMetadata{};
    if (entry.source) spec.sourceA.still = entry.source->imageOptions();
    spec.sourceA.rangeStartFrame = 0;
    spec.sourceA.rangeEndFrame = std::max<qint64>(0, spec.sourceA.metadata.effectiveFrameCount() - 1);
    spec.annotations = entry.id == m_annotationSourceId ? m_annotations->annotations() : entry.annotations;
    return spec;
}

void MainWindow::exportReviewNotesBatch()
{
    if (notesPdfInProgress()) return;
    if (m_project->entries().isEmpty()) {
        QMessageBox::information(this, tr("Export Notes PDFs for Playlist"), tr("The playlist is empty."));
        return;
    }
    QVector<BatchNotesClip> clips;
    for (const project::SourceEntry& entry : m_project->entries()) {
        BatchNotesClip clip;
        clip.id = entry.id;
        clip.name = entry.displayName.isEmpty() ? QFileInfo(entry.storedPath).fileName() : entry.displayName;
        clip.annotations = entry.id == m_annotationSourceId ? m_annotations->annotations() : entry.annotations;
        if (entry.missing || entry.availability == project::SourceAvailability::Missing)
            clip.unavailableReason = tr("media missing");
        else if (entry.availability == project::SourceAvailability::Error || !entry.source)
            clip.unavailableReason = tr("cannot be read");
        else if (entry.source->metadata().effectiveFrameCount() <= 0)
            clip.unavailableReason = tr("still being read");
        clips.append(std::move(clip));
    }
    // In the project's save location, or beside the project, as the review's
    // other files are.
    const QString folder = projectExportFolderAvailable()       ? m_project->exportFolder()
                         : !m_project->filePath().isEmpty() ? QFileInfo(m_project->filePath()).absolutePath()
                         : !m_lastMediaDirectory.isEmpty()  ? m_lastMediaDirectory
                                                            : QDir::homePath();
    BatchNotesPdfDialog dialog(std::move(clips), folder, m_tracingPaper, this);
    dialog.setOpenFolderWhenDone(m_settings->openExportsWhenDone());
    dialog.setIncludeNoteText(m_settings->reviewNotesIncludeText());
    if (dialog.exec() != QDialog::Accepted) return;
    m_settings->setOpenExportsWhenDone(dialog.openFolderWhenDone());
    m_settings->setReviewNotesIncludeText(dialog.includeNoteText());
    const QVector<QUuid> chosen = dialog.chosenClips();
    const QString target = dialog.folder();
    if (chosen.isEmpty() || target.isEmpty()) return;
    if (!QDir().mkpath(target)) {
        QMessageBox::critical(this, tr("Export Notes PDFs for Playlist"),
                              tr("The folder could not be created:\n%1").arg(QDir::toNativeSeparators(target)));
        return;
    }

    // One question for every PDF already there: the names the batch would
    // write when replacing, numbered ones for repeated clips included.
    int existing = 0;
    if (const std::unique_ptr<NotesPdfBatch> plan = planNotesPdfs(chosen, target, dialog.options(), true))
        for (const NotesPdfBatch::Item& item : std::as_const(plan->items))
            if (QFileInfo::exists(item.outputPath)) ++existing;
    bool replace = true;
    if (existing > 0) {
        QMessageBox box(QMessageBox::Question, tr("Export Notes PDFs for Playlist"),
                        tr("%n of these PDFs already exist in the folder.", nullptr, existing),
                        QMessageBox::NoButton, this);
        QPushButton* replaceButton = box.addButton(tr("Replace"), QMessageBox::AcceptRole);
        QPushButton* keepButton = box.addButton(tr("Keep Both"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setInformativeText(tr("Keep Both saves the new ones as \"name (2).pdf\"."));
        box.setDefaultButton(keepButton);
        box.exec();
        if (box.clickedButton() == replaceButton) replace = true;
        else if (box.clickedButton() == keepButton) replace = false;
        else return;
    }
    exportNotesPdfs(chosen, target, dialog.options(), replace, dialog.openFolderWhenDone());
}

bool MainWindow::exportNotesPdfs(const QVector<QUuid>& clips, const QString& folder,
                                 const exporter::ReviewNotesOptions& options, bool replaceExisting,
                                 bool openFolderWhenDone)
{
    if (notesPdfInProgress() || folder.isEmpty()) return false;
    std::unique_ptr<NotesPdfBatch> batch = planNotesPdfs(clips, folder, options, replaceExisting);
    batch->openFolderWhenDone = openFolderWhenDone;
    if (batch->items.isEmpty()) {
        m_lastNotesBatchSaved.clear();
        m_lastNotesBatchFailed = batch->failed;
        return false;
    }
    m_notesBatch = std::move(batch);
    m_notesPdfProgress = new ExportProgressDialog(QStringLiteral("PDF"), tr("Exporting notes PDFs"),
                                                  QDir::toNativeSeparators(m_notesBatch->folder), this);
    m_notesPdfProgress->setWindowModality(Qt::NonModal);
    connect(m_notesPdfProgress, &ExportProgressDialog::canceled, this, [this] {
        if (!m_notesBatch) return;
        m_notesBatch->cancelRequested = true;
        if (m_notesPdfJob) m_notesPdfJob->cancel();
    });
    m_notesPdfProgress->show();
    updateTransportEnabled();
    startNextNotesPdf();
    return true;
}

std::unique_ptr<NotesPdfBatch> MainWindow::planNotesPdfs(const QVector<QUuid>& clips, const QString& folder,
                                                         const exporter::ReviewNotesOptions& options,
                                                         bool replaceExisting) const
{
    auto batch = std::make_unique<NotesPdfBatch>();
    batch->options = options;
    batch->folder = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    QSet<QString> taken; // two clips with the same file name get (2), (3)...
    for (const QUuid& id : clips) {
        const int index = m_project->indexForId(id);
        if (index < 0) continue;
        const project::SourceEntry& entry = m_project->entries().at(index);
        NotesPdfBatch::Item item;
        item.name = entry.displayName.isEmpty() ? QFileInfo(entry.storedPath).fileName() : entry.displayName;
        item.spec = notesPdfSpec(entry);
        if (exporter::ReviewNotesJob::framesToInclude(item.spec.annotations, options.includeResolved,
                                                      options.includeNoteText).isEmpty()) {
            batch->failed.append(tr("%1: nothing to list").arg(item.name));
            continue;
        }
        if (!entry.source || item.spec.sourceA.metadata.effectiveFrameCount() <= 0) {
            batch->failed.append(tr("%1: the media cannot be read").arg(item.name));
            continue;
        }
        item.header = notesPdfHeader(item.spec, options.includeResolved);
        const QString base = exportBaseName(item.spec.sourceA) + QStringLiteral("_review_notes");
        QString path = QDir(batch->folder).filePath(base + QStringLiteral(".pdf"));
        for (int n = 2; taken.contains(path.toCaseFolded()) || (!replaceExisting && QFileInfo::exists(path)); ++n)
            path = QDir(batch->folder).filePath(QStringLiteral("%1 (%2).pdf").arg(base).arg(n));
        taken.insert(path.toCaseFolded());
        item.outputPath = path;
        batch->items.append(std::move(item));
    }
    return batch;
}

void MainWindow::startNextNotesPdf()
{
    if (!m_notesBatch) return;
    if (m_notesBatch->cancelRequested) { finishNotesPdfBatch(true); return; }
    if (m_notesBatch->next >= m_notesBatch->items.size()) { finishNotesPdfBatch(false); return; }
    const int index = m_notesBatch->next;
    const int total = int(m_notesBatch->items.size());
    const NotesPdfBatch::Item& item = m_notesBatch->items.at(index);
    exporter::ReviewNotesOptions options = m_notesBatch->options;
    options.outputPath = item.outputPath;
    m_notesPdfJob = std::make_unique<exporter::ReviewNotesJob>(item.spec, item.header, options, this);
    if (m_notesPdfProgress) {
        m_notesPdfProgress->setLabelText(tr("Clip %1 of %2: %3").arg(index + 1).arg(total).arg(item.name));
        m_notesPdfProgress->setValue(100 * index / total);
    }
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::progress, this, [this, index, total](int percent) {
        if (m_notesPdfProgress) m_notesPdfProgress->setValue((100 * index + percent) / total);
    });
    // Each step joins the finished worker, then moves on from the event loop:
    // the job that is reporting is not replaced inside its own signal.
    const auto advance = [this] {
        m_notesPdfJob->wait();
        if (!m_notesBatch) return;
        ++m_notesBatch->next;
        QTimer::singleShot(0, this, &MainWindow::startNextNotesPdf);
    };
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::completed, this, [this, advance](const QString& path, int) {
        if (m_notesBatch) m_notesBatch->saved.append(path);
        advance();
    });
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::failed, this,
            [this, advance, name = item.name](const QString& message) {
        if (m_notesBatch) m_notesBatch->failed.append(QStringLiteral("%1: %2").arg(name, message));
        advance();
    });
    connect(m_notesPdfJob.get(), &exporter::ReviewNotesJob::cancelled, this, [this] {
        m_notesPdfJob->wait();
        if (m_notesBatch) finishNotesPdfBatch(true);
    });
    m_notesPdfJob->start();
}

void MainWindow::finishNotesPdfBatch(bool cancelled)
{
    if (!m_notesBatch) return;
    const std::unique_ptr<NotesPdfBatch> batch = std::move(m_notesBatch);
    m_lastNotesBatchSaved = batch->saved;
    m_lastNotesBatchFailed = batch->failed;
    if (m_notesPdfProgress) {
        m_notesPdfProgress->finish();
        m_notesPdfProgress->deleteLater();
        m_notesPdfProgress = nullptr;
    }
    updateTransportEnabled();
    const QString folder = QDir::toNativeSeparators(batch->folder);
    statusBar()->showMessage(cancelled
        ? tr("Export cancelled after %n PDF(s).", nullptr, int(batch->saved.size()))
        : tr("Exported %n notes PDF(s) to %1", nullptr, int(batch->saved.size())).arg(folder), 10000);
    if (!cancelled && !batch->failed.isEmpty())
        QMessageBox::warning(this, tr("Export Notes PDFs for Playlist"),
                             tr("These clips were not exported:\n\n%1").arg(batch->failed.join(QLatin1Char('\n'))));
    if (!cancelled && batch->openFolderWhenDone && !batch->saved.isEmpty()) {
        m_lastOpenedExport = batch->folder;
        if (!qEnvironmentVariableIsSet("ATK_NO_OPEN_EXPORTS")) // tests
            QDesktopServices::openUrl(QUrl::fromLocalFile(batch->folder));
    }
}

bool MainWindow::cancelExportForProjectChange()
{
    if (!exportInProgress() && !notesPdfInProgress()) return true;
    if (QMessageBox::question(this, tr("Export in Progress"),
        tr("Cancel the export before changing projects?"), QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No) != QMessageBox::Yes) return false;
    // Stop work, then join the worker, before the UI state it reports to goes.
    if (m_exportJob) { m_exportJob->cancel(); m_exportJob->wait(); }
    if (m_notesPdfJob) { m_notesPdfJob->cancel(); m_notesPdfJob->wait(); }
    if (m_notesBatch) { m_lastNotesBatchSaved = m_notesBatch->saved; m_notesBatch.reset(); }
    if (m_exportProgress) { m_exportProgress->finish(); m_exportProgress->deleteLater(); m_exportProgress = nullptr; }
    if (m_notesPdfProgress) { m_notesPdfProgress->finish(); m_notesPdfProgress->deleteLater(); m_notesPdfProgress = nullptr; }
    updateTransportEnabled();
    return true;
}

void MainWindow::showWelcomeIfFirstRun()
{
    const QString current = QString::fromLatin1(version::kString);
    if (m_settings->welcomeShownVersion() == current) return;
    m_settings->setWelcomeShownVersion(current);
    m_settings->sync();
    // Queued so the main window has painted before the dialog appears over it.
    QTimer::singleShot(0, this, &MainWindow::showWelcome);
}

void MainWindow::showWelcome()
{
    auto* dialog = new WelcomeDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!droppedMediaPaths(event->mimeData()).isEmpty()) event->acceptProposedAction();
    else event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QStringList paths = droppedMediaPaths(event->mimeData());
    if (paths.isEmpty()) { event->ignore(); return; }
    event->acceptProposedAction();
    m_lastMediaDirectory = QFileInfo(paths.first()).absolutePath();
    addMediaFiles(paths);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_viewer || watched == m_annotationToolbar) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::Hide:
            placeBrushSizePill();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::placeBrushSizePill()
{
    if (!m_brushSizePill) return;
    // Shown with the drawing tools, over the viewer's left edge and centred on
    // it, as long as the viewer has room for it.
    const QRect viewer = m_viewer->geometry();
    const QSize size = m_brushSizePill->sizeHint();
    const bool show = m_annotationToolbar->isVisibleTo(this) && m_viewer->isVisibleTo(this)
        && viewer.height() >= size.height() + 2 * theme::metrics::spacingLarge;
    if (show) {
        m_brushSizePill->resize(size);
        m_brushSizePill->move(viewer.left() + theme::metrics::spacingLarge,
                              viewer.top() + (viewer.height() - size.height()) / 2);
        m_brushSizePill->raise();
    }
    m_brushSizePill->setVisible(show);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (isVideoFullScreen()) exitVideoFullScreen();
    if (!cancelExportForProjectChange()) { event->ignore(); return; }
    if (confirmDiscardChanges()) { saveApplicationLayout(); event->accept(); }
    else event->ignore();
}

bool MainWindow::isVideoFullScreen() const
{
    return m_videoFullscreenWindow && m_videoFullscreenWindow->isVisible();
}

void MainWindow::enterVideoFullScreen()
{
    QAction* action = m_commands->action(CommandId::ToggleVideoFullScreen);
    if (isVideoFullScreen() || !m_playback->hasMedia()) {
        if (action) {
            QSignalBlocker blocker(action);
            action->setChecked(isVideoFullScreen());
        }
        return;
    }

    const bool composite = isComparisonActive()
        && m_compare->layout() != playback::CompareLayout::SideBySide
        && m_compare->layout() != playback::CompareLayout::Stacked;
    m_fullscreenPresentation = isComparisonActive()
        ? static_cast<QWidget*>(composite ? static_cast<QWidget*>(m_compareComposite)
                                         : static_cast<QWidget*>(m_compareSplitter))
        : static_cast<QWidget*>(m_viewer);
    m_normalViewerTransform = m_viewer->transform();
    m_normalViewerBTransform = m_viewerB->transform();
    m_normalCompositeTransform = m_compareComposite->transform();
    m_normalViewerSize = m_fullscreenPresentation->size();
    if (isComparisonActive()) m_compareLayout->removeWidget(m_fullscreenPresentation);
    else m_centralLayout->removeWidget(m_viewer);

    if (!m_videoFullscreenWindow) {
        m_videoFullscreenWindow = new VideoFullscreenWindow(this);
        m_videoFullscreenWindow->installCommandActions(m_commands->allActions());
        connect(m_videoFullscreenWindow, &VideoFullscreenWindow::exitRequested,
                this, &MainWindow::exitVideoFullScreen);
    }

    QScreen* targetScreen = m_fullscreenPresentation->screen();
    if (!targetScreen) targetScreen = screen();
    m_viewer->setVideoOnlyPresentation(true);
    m_viewerB->setVideoOnlyPresentation(true);
    m_compareComposite->setVideoOnlyPresentation(true);
    m_videoFullscreenWindow->hostPresentation(m_fullscreenPresentation);
    if (composite) m_compareComposite->fitImage();
    else { m_viewer->fitImage(); if (isComparisonActive()) m_viewerB->fitImage(); }
    m_videoFullscreenWindow->winId();
    if (targetScreen && m_videoFullscreenWindow->windowHandle()) {
        m_videoFullscreenWindow->windowHandle()->setScreen(targetScreen);
    }
    m_videoFullscreenWindow->showFullScreen();
    m_videoFullscreenWindow->activateWindow();
    m_fullscreenPresentation->setFocus(Qt::ShortcutFocusReason);

    if (action) {
        QSignalBlocker blocker(action);
        action->setChecked(true);
    }
}

void MainWindow::exitVideoFullScreen()
{
    QAction* action = m_commands ? m_commands->action(CommandId::ToggleVideoFullScreen) : nullptr;
    if (!isVideoFullScreen()) {
        if (action) {
            QSignalBlocker blocker(action);
            action->setChecked(false);
        }
        return;
    }

    m_videoFullscreenWindow->releasePresentation();
    m_videoFullscreenWindow->hide();
    if (isComparisonActive()) {
        m_fullscreenPresentation->setParent(m_compareHost);
        m_compareLayout->insertWidget(1, m_fullscreenPresentation, 1);
    } else {
        m_viewer->setParent(m_playerPage);
        m_centralLayout->insertWidget(0, m_viewer, 1);
    }
    m_fullscreenPresentation->resize(m_normalViewerSize);
    m_centralLayout->activate();
    m_viewer->setVideoOnlyPresentation(false);
    m_viewerB->setVideoOnlyPresentation(false);
    m_compareComposite->setVideoOnlyPresentation(false);
    m_viewer->restoreTransform(m_normalViewerTransform);
    m_viewerB->restoreTransform(m_normalViewerBTransform);
    m_compareComposite->restoreTransform(m_normalCompositeTransform);
    m_viewer->show();
    m_fullscreenPresentation->show();
    m_fullscreenPresentation = nullptr;
    activateWindow();

    if (action) {
        QSignalBlocker blocker(action);
        action->setChecked(false);
    }
}

bool MainWindow::isComparisonActive() const
{
    return m_compare && m_compare->isActive();
}

bool MainWindow::sourceUsableForComparison(int index) const
{
    if (index < 0 || index >= m_project->entries().size()) return false;
    const auto& entry = m_project->entries().at(index);
    return entry.source && entry.availability != project::SourceAvailability::Missing
        && entry.availability != project::SourceAvailability::Error;
}

int MainWindow::defaultComparisonBIndex(int sourceAIndex) const
{
    for (int i = sourceAIndex + 1; i < m_project->entries().size(); ++i)
        if (sourceUsableForComparison(i)) return i;
    for (int i = sourceAIndex - 1; i >= 0; --i)
        if (sourceUsableForComparison(i)) return i;
    return -1;
}

void MainWindow::enterComparison()
{
    QAction* toggle = m_commands->action(CommandId::ToggleComparison);
    if (isComparisonActive()) return;
    // Drawing targets one source's frames; comparison shows two.
    if (m_viewer->annotateMode()) setAnnotateMode(false);
    if (isVideoFullScreen()) exitVideoFullScreen();
    const int a = m_project->activeIndex();
    const int b = defaultComparisonBIndex(a);
    if (!sourceUsableForComparison(a) || b < 0
        || !m_compare->setSources(m_project->entries().at(a).id, m_project->entries().at(b).id)) {
        if (toggle) { QSignalBlocker blocker(toggle); toggle->setChecked(false); }
        statusBar()->showMessage(tr("A/B Comparison requires two available media sources."), 5000);
        return;
    }

    m_compare->setActive(true);
    m_compare->setAudioMode(playback::CompareAudioMode::SourceA);
    m_compare->setActivePane(playback::ComparePane::A);
    m_centralLayout->removeWidget(m_viewer);
    m_compareSplitter->insertWidget(0, m_viewer);
    m_compareSplitter->setSizes({1, 1});
    m_centralLayout->insertWidget(0, m_compareHost, 1);
    m_compareHost->show();
    m_compareLane = std::make_unique<playback::CompareVideoLane>();
    connect(m_compareLane.get(), &playback::CompareVideoLane::frameChanged,
            m_viewerB, &ViewerWidget::setFrame);
    connect(m_compareLane.get(), &playback::CompareVideoLane::frameChanged,
            m_compareComposite, &ComparisonCompositeWidget::setFrameB);
    connect(m_compareLane.get(), &playback::CompareVideoLane::loadingChanged, this,
            [this](bool loading) { if (loading) m_viewerB->setLoading(); });
    connect(m_compareLane.get(), &playback::CompareVideoLane::readyChanged, this,
            [this](bool ready) {
                if (!ready || !m_compareLane) return;
                const auto& metadata = m_compareLane->metadata();
                m_viewerB->setSourceAspectRatio(metadata.resolution.height() > 0
                    ? static_cast<double>(metadata.resolution.width()) * metadata.pixelAspectRatio
                        / metadata.resolution.height() : 0.0);
                m_viewerB->resetNavigationToFit();
                synchronizeComparison(m_playback->currentVideoFrame());
            });
    connect(m_compareLane.get(), &playback::CompareVideoLane::errorOccurred, this,
            [this](const QString& message) { m_viewerB->setError(message); });
    openComparisonSourceB();
    refreshComparisonUi();
    setComparisonLayout(m_compare->layout());
    if (toggle) { QSignalBlocker blocker(toggle); toggle->setChecked(true); }
    updateTransportEnabled();
}

void MainWindow::exitComparison()
{
    if (!isComparisonActive()) return;
    if (isVideoFullScreen()) exitVideoFullScreen();
    m_playback->clearComparisonAudioSource();
    m_playback->clearComparisonWaveformSource(m_compare->generation());
    m_compareLane.reset();
    m_compareHost->hide();
    m_centralLayout->removeWidget(m_compareHost);
    m_viewer->setParent(m_playerPage);
    m_centralLayout->insertWidget(0, m_viewer, 1);
    m_viewer->setCornerLabel(QString());
    m_viewer->setStyleSheet(QString());
    m_viewerB->setStyleSheet(QString());
    m_viewerB->clear();
    m_compare->setActive(false);
    if (QAction* toggle = m_commands->action(CommandId::ToggleComparison)) {
        QSignalBlocker blocker(toggle); toggle->setChecked(false);
    }
    updateTransportEnabled();
}

void MainWindow::setComparisonLayout(playback::CompareLayout layout)
{
    if (!m_compareSplitter) return;
    const bool dual = layout == playback::CompareLayout::SideBySide
        || layout == playback::CompareLayout::Stacked;
    m_compareSplitter->setVisible(dual);
    m_compareComposite->setVisible(!dual);
    if (dual) {
        m_compareSplitter->setOrientation(layout == playback::CompareLayout::SideBySide
            ? Qt::Horizontal : Qt::Vertical);
        m_compareSplitter->setSizes({1, 1});
    } else {
        m_compareComposite->setMode(layout);
    }
    for (auto pair : {std::pair{CommandId::CompareSideBySide, playback::CompareLayout::SideBySide},
                      std::pair{CommandId::CompareStacked, playback::CompareLayout::Stacked},
                      std::pair{CommandId::CompareWipe, playback::CompareLayout::Wipe},
                      std::pair{CommandId::CompareBlend, playback::CompareLayout::Blend},
                      std::pair{CommandId::CompareDifference, playback::CompareLayout::Difference}}) {
        QAction* action = m_commands->action(pair.first); QSignalBlocker blocker(action);
        action->setChecked(layout == pair.second);
    }
    refreshComparisonUi();
}

bool MainWindow::selectComparisonSourceA(const QUuid& id)
{
    if (!isComparisonActive() || id.isNull() || id == m_compare->sourceBId()) {
        refreshComparisonUi();
        return false;
    }
    const int index = m_project->indexForId(id);
    if (!sourceUsableForComparison(index)
        || !m_compare->setSources(id, m_compare->sourceBId())) {
        refreshComparisonUi();
        return false;
    }
    activatePlaylistIndex(index, m_playback->isPlaying());
    refreshComparisonUi();
    return m_compare->sourceAId() == id && m_project->currentSourceId() == id;
}

bool MainWindow::selectComparisonSourceB(const QUuid& id)
{
    if (!isComparisonActive() || id.isNull() || id == m_compare->sourceAId()) {
        refreshComparisonUi(); return false;
    }
    const int index = m_project->indexForId(id);
    if (!sourceUsableForComparison(index)) { refreshComparisonUi(); return false; }
    const QUuid previous = m_compare->sourceBId();
    if (!m_compare->setSources(m_compare->sourceAId(), id)) {
        refreshComparisonUi();
        return false;
    }
    if (previous != id) m_compare->setSourceBOffsetUs(0);
    openComparisonSourceB();
    if (m_compare->audioMode() == playback::CompareAudioMode::SourceB)
        applyComparisonAudioMode(playback::CompareAudioMode::SourceB);
    refreshComparisonUi();
    return m_compare->sourceBId() == id;
}

void MainWindow::openComparisonSourceB()
{
    if (!isComparisonActive() || !m_compareLane) return;
    const int index = m_project->indexForId(m_compare->sourceBId());
    if (!sourceUsableForComparison(index)) return;
    const auto& entry = m_project->entries().at(index);
    // Without a playback range, the lane runs to the last frame it finds when
    // it opens the file. The playlist's probe may not have finished yet, and
    // its frame count of 0 would hold B on its first frame (any offset).
    const qint64 start = entry.playbackRange.enabled ? entry.playbackRange.startFrame : 0;
    const qint64 end = entry.playbackRange.enabled ? entry.playbackRange.endFrame : -1;
    m_viewerB->setCornerLabel(tr("B\n%1").arg(entry.displayName));
    m_compareLane->open(entry.id, entry.source, start, end);
}

void MainWindow::synchronizeComparison(const media::VideoFrame& sourceAFrame)
{
    if (!isComparisonActive() || !m_compareLane || !sourceAFrame.isValid()) return;
    const int bIndex = m_project->indexForId(m_compare->sourceBId());
    if (!sourceUsableForComparison(bIndex)) return;
    m_compareLane->synchronizeToSourceFrame(
        sourceAFrame, m_playback->metadata(), m_timeline->effectiveStartFrame(),
        m_compare->sourceBOffsetUs());
}

void MainWindow::refreshComparisonUi()
{
    if (!isComparisonActive()) return;
    m_compareBar->refresh(m_compare->sourceAId(), m_compare->sourceBId());
    const int a = m_project->indexForId(m_compare->sourceAId());
    if (a >= 0) m_viewer->setCornerLabel(tr("A\n%1").arg(m_project->entries().at(a).displayName));
    const int b = m_project->indexForId(m_compare->sourceBId());
    const bool aAudio = a >= 0 && m_project->entries().at(a).source
        && m_project->entries().at(a).source->metadata().hasAudio;
    const bool bAudio = b >= 0 && m_project->entries().at(b).source
        && m_project->entries().at(b).source->metadata().hasAudio;
    m_compareBar->setAudioState(m_compare->audioMode(), m_compare->externalAudioPath(),
                                aAudio, bAudio);
    m_compareBar->setComparisonState(m_compare->layout(),
        framesForOffsetUs(m_compare->sourceBOffsetUs()),
        framesForOffsetUs(m_compare->externalAudioOffsetUs()),
        m_compare->wipePosition(), m_compare->blendAmount(),
        !m_compare->externalAudioPath().isEmpty());
    if (QAction* clear = m_commands->action(CommandId::ClearExternalAudio))
        clear->setEnabled(!m_compare->externalAudioPath().isEmpty());
}

qint64 MainWindow::offsetUsForFrames(int frames) const
{
    const qint64 magnitude = playback::CompareSession::frameTimeUs(
        std::abs(static_cast<qint64>(frames)), m_playback->metadata().frameRate);
    return frames < 0 ? -magnitude : magnitude;
}

int MainWindow::framesForOffsetUs(qint64 offsetUs) const
{
    const auto& rate = m_playback->metadata().frameRate;
    if (!rate.isValid()) return 0;
    const long double frames = static_cast<long double>(offsetUs) * rate.numerator
        / (static_cast<long double>(rate.denominator) * 1'000'000.0L);
    return static_cast<int>(std::llround(frames));
}

void MainWindow::setSourceBOffsetFrames(int frames)
{
    if (!isComparisonActive()) return;
    m_compare->setSourceBOffsetUs(offsetUsForFrames(frames));
    synchronizeComparison(m_playback->currentVideoFrame());
    reanchorComparisonFollowers(true);
    refreshComparisonUi();
}

void MainWindow::setExternalAudioOffsetFrames(int frames)
{
    if (!isComparisonActive()) return;
    m_compare->setExternalAudioOffsetUs(offsetUsForFrames(frames));
    reanchorComparisonFollowers(true);
    refreshComparisonUi();
}

void MainWindow::reanchorComparisonFollowers(bool waveformMappingOnly)
{
    Q_UNUSED(waveformMappingOnly);
    if (!isComparisonActive()) return;
    const auto mode = m_compare->audioMode();
    if (mode == playback::CompareAudioMode::SourceA) return;
    QString path; qint64 providerOriginUs = 0; bool hasAudio = false;
    if (mode == playback::CompareAudioMode::SourceB) {
        const int index = m_project->indexForId(m_compare->sourceBId());
        if (index < 0) return;
        const auto& entry = m_project->entries().at(index);
        path = entry.source ? entry.source->filePath() : QString();
        hasAudio = entry.source && entry.source->metadata().hasAudio;
        const qint64 start = entry.playbackRange.enabled ? entry.playbackRange.startFrame : 0;
        providerOriginUs = playback::CompareSession::frameTimeUs(
            start, entry.source->metadata().frameRate) + m_compare->sourceBOffsetUs();
    } else {
        path = m_compare->externalAudioPath();
        hasAudio = !path.isEmpty();
        providerOriginUs = m_compare->externalAudioOffsetUs();
    }
    const qint64 masterOriginUs = playback::CompareSession::frameTimeUs(
        m_timeline->effectiveStartFrame(), m_playback->metadata().frameRate);
    m_playback->setComparisonAudioSource(path, providerOriginUs, masterOriginUs, hasAudio);
    m_playback->updateComparisonWaveformMapping(providerOriginUs - masterOriginUs,
                                                m_compare->generation());
}

void MainWindow::applyComparisonAudioMode(playback::CompareAudioMode mode)
{
    if (!isComparisonActive()) return;
    m_compare->setAudioMode(mode);
    if (mode == playback::CompareAudioMode::SourceA) {
        m_playback->clearComparisonAudioSource();
        m_playback->clearComparisonWaveformSource(m_compare->generation());
    } else {
        QString path; qint64 providerOriginUs = 0; bool hasAudio = false;
        if (mode == playback::CompareAudioMode::SourceB) {
            const int index = m_project->indexForId(m_compare->sourceBId());
            if (index >= 0) {
                const auto& entry = m_project->entries().at(index);
                path = entry.source ? entry.source->filePath() : QString();
                hasAudio = entry.source && entry.source->metadata().hasAudio;
                const qint64 startFrame = entry.playbackRange.enabled
                    ? entry.playbackRange.startFrame : 0;
                providerOriginUs = playback::CompareSession::frameTimeUs(
                    startFrame, entry.source->metadata().frameRate) + m_compare->sourceBOffsetUs();
            }
        } else {
            path = m_compare->externalAudioPath();
            hasAudio = !path.isEmpty();
            providerOriginUs = m_compare->externalAudioOffsetUs();
        }
        const qint64 masterOriginUs = playback::CompareSession::frameTimeUs(
            m_timeline->effectiveStartFrame(), m_playback->metadata().frameRate);
        m_playback->setComparisonAudioSource(path, providerOriginUs, masterOriginUs, hasAudio);
        m_playback->setComparisonWaveformSource(path, hasAudio, providerOriginUs,
                                                masterOriginUs, m_compare->generation());
    }
    refreshComparisonUi();
}

void MainWindow::loadExternalAudio()
{
    if (!isComparisonActive()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Load External Audio"), QString(),
        tr("Audio and Media (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.mp4 *.mov);;All Files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!media::CompareAudioWorker::validateSource(path, &error)) {
        QMessageBox::warning(this, tr("External Audio"),
                             tr("The selected file does not contain a usable audio stream."));
        return;
    }
    m_compare->setExternalAudioPath(path);
    m_compare->setExternalAudioOffsetUs(0);
    applyComparisonAudioMode(playback::CompareAudioMode::External);
}

void MainWindow::clearExternalAudio()
{
    if (!isComparisonActive()) return;
    m_compare->setExternalAudioPath(QString());
    m_compare->setExternalAudioOffsetUs(0);
    if (m_compare->audioMode() == playback::CompareAudioMode::External)
        applyComparisonAudioMode(playback::CompareAudioMode::External);
    else refreshComparisonUi();
}

ViewerWidget* MainWindow::activeViewer() const
{
    return isComparisonActive() && m_compare->activePane() == playback::ComparePane::B
        ? m_viewerB : m_viewer;
}

api::ApiResponse MainWindow::handleApiApplicationCommand(
    const QString& command, const QJsonObject& params)
{
    const auto fail = [](const QString& message) { return api::ApiResponse::failure(message); };
    const auto localFile = [&](const QString& key, bool mustExist, QString* out) {
        const QJsonValue value = params.value(key);
        if (!value.isString() || value.toString().isEmpty()) return false;
        const QString path = value.toString();
        const QFileInfo info(path);
        if (!info.isAbsolute() || path.contains(QStringLiteral("://"))) return false;
        if (mustExist && (!info.exists() || !info.isFile())) return false;
        *out = info.absoluteFilePath();
        return true;
    };
    const auto readBurnIns = [&](exporter::ExportBurnIns* out, QString* error) {
        if (!params.contains(QStringLiteral("burnIns"))) return true;
        const QJsonValue value = params.value(QStringLiteral("burnIns"));
        if (!value.isObject()) { *error = QStringLiteral("burnIns must be an object"); return false; }
        const QJsonObject object = value.toObject();
        const auto read = [&](const QString& key, bool* target) {
            if (!object.contains(key)) return true;
            if (!object.value(key).isBool()) { *error = key + QStringLiteral(" must be a boolean"); return false; }
            *target = object.value(key).toBool(); return true;
        };
        const auto readInt = [&](const QString& key, int maximum, int* target) {
            if (!object.contains(key)) return true;
            const QJsonValue v = object.value(key);
            if (!v.isDouble() || v.toDouble() < 0 || v.toDouble() > maximum || v.toDouble() != std::floor(v.toDouble())) {
                *error = key + QStringLiteral(" must be an integer from 0 to %1").arg(maximum);
                return false;
            }
            *target = v.toInt();
            return true;
        };
        if (!readInt(QStringLiteral("annotationHoldFrames"), 240, &out->annotationHoldFrames)
            || !readInt(QStringLiteral("annotationFreezeFrames"), 240, &out->annotationFreezeFrames)
            || !readInt(QStringLiteral("tracingPaper"), 100, &out->tracingPaperPercent))
            return false;
        return read(QStringLiteral("frameNumber"), &out->frameNumber)
            && read(QStringLiteral("bookmarkLabels"), &out->bookmarkLabels)
            && read(QStringLiteral("bookmarkNotes"), &out->bookmarkNotes)
            && read(QStringLiteral("annotations"), &out->annotations)
            && read(QStringLiteral("annotationNotes"), &out->annotationNotes);
    };
    const auto mayDiscard = [&] {
        return !m_project->needsSavePrompt()
            || params.value(QStringLiteral("discardUnsaved")).toBool(false);
    };
    const auto sourceAvailability = [](project::SourceAvailability value) {
        switch (value) {
        case project::SourceAvailability::Unknown: return QStringLiteral("unknown");
        case project::SourceAvailability::Probing: return QStringLiteral("probing");
        case project::SourceAvailability::Ready: return QStringLiteral("ready");
        case project::SourceAvailability::Missing: return QStringLiteral("missing");
        case project::SourceAvailability::Error: return QStringLiteral("error");
        }
        return QStringLiteral("unknown");
    };
    const auto compareLayoutName = [](playback::CompareLayout value) {
        switch (value) {
        case playback::CompareLayout::SideBySide: return QStringLiteral("side_by_side");
        case playback::CompareLayout::Stacked: return QStringLiteral("stacked");
        case playback::CompareLayout::Wipe: return QStringLiteral("wipe");
        case playback::CompareLayout::Blend: return QStringLiteral("blend");
        case playback::CompareLayout::Difference: return QStringLiteral("difference");
        }
        return QStringLiteral("side_by_side");
    };
    const auto audioModeName = [](playback::CompareAudioMode value) {
        switch (value) {
        case playback::CompareAudioMode::SourceA: return QStringLiteral("a");
        case playback::CompareAudioMode::SourceB: return QStringLiteral("b");
        case playback::CompareAudioMode::External: return QStringLiteral("external");
        }
        return QStringLiteral("a");
    };

    if (command == QLatin1StringView("get_status")) {
        QJsonObject reviewRange{
            {QStringLiteral("start"), static_cast<double>(m_timeline->effectiveStartFrame())},
            {QStringLiteral("end"), static_cast<double>(m_timeline->effectiveEndFrame())},
        };
        QJsonObject comparison{
            {QStringLiteral("enabled"), isComparisonActive()},
            {QStringLiteral("sourceAId"), m_compare->sourceAId().toString(QUuid::WithoutBraces)},
            {QStringLiteral("sourceBId"), m_compare->sourceBId().toString(QUuid::WithoutBraces)},
            {QStringLiteral("view"), compareLayoutName(m_compare->layout())},
            {QStringLiteral("bOffsetUs"), static_cast<double>(m_compare->sourceBOffsetUs())},
            {QStringLiteral("bOffsetFrames"), framesForOffsetUs(m_compare->sourceBOffsetUs())},
            {QStringLiteral("audioMode"), audioModeName(m_compare->audioMode())},
            {QStringLiteral("externalAudioLoaded"), !m_compare->externalAudioPath().isEmpty()},
            {QStringLiteral("externalAudioOffsetUs"),
             static_cast<double>(m_compare->externalAudioOffsetUs())},
        };
        return api::ApiResponse::success({
            {QStringLiteral("applicationVersion"), QString::fromLatin1(version::kString)},
            {QStringLiteral("projectName"), m_project->name()},
            {QStringLiteral("projectPath"), m_project->filePath()},
            {QStringLiteral("projectDirty"), m_project->isModified()},
            {QStringLiteral("activeSourceId"),
             m_project->currentSourceId().toString(QUuid::WithoutBraces)},
            {QStringLiteral("activeSourceIndex"), m_project->activeIndex()},
            {QStringLiteral("reviewRange"), reviewRange},
            {QStringLiteral("comparison"), comparison},
            {QStringLiteral("export"), QJsonObject{
                 {QStringLiteral("jobId"), m_apiExportJobId},
                 {QStringLiteral("state"), m_apiExportState},
                 {QStringLiteral("progress"), m_apiExportProgress / 100.0},
             }},
        });
    }

    if (command == QLatin1StringView("open_media")) {
        QString path;
        if (!localFile(QStringLiteral("path"), true, &path))
            return fail(QStringLiteral("path must be an absolute existing local file"));
        if (!mayDiscard()) return fail(QStringLiteral("current project has unsaved changes"));
        if (exportInProgress()) return fail(QStringLiteral("an export is active"));
        exitComparison();
        ++m_projectGeneration;
        m_restoringSourceState = true;
        m_project->clear();
        m_project->setName(QStringLiteral("Untitled"));
        m_project->setFilePath({});
        m_restoringSourceState = false;
        addMediaFiles({path}, false);
        return api::ApiResponse::success({
            {QStringLiteral("accepted"), true}, {QStringLiteral("path"), path}});
    }

    if (command == QLatin1StringView("open_project")) {
        QString path;
        if (!localFile(QStringLiteral("path"), true, &path)
            || !path.endsWith(QStringLiteral(".atkproj"), Qt::CaseInsensitive))
            return fail(QStringLiteral("path must be an absolute existing .atkproj file"));
        if (!mayDiscard()) return fail(QStringLiteral("current project has unsaved changes"));
        if (exportInProgress()) return fail(QStringLiteral("an export is active"));
        m_suppressProjectOpenError = true;
        const bool opened = openProjectFile(path);
        m_suppressProjectOpenError = false;
        return opened ? api::ApiResponse::success({{QStringLiteral("accepted"), true},
                                                   {QStringLiteral("path"), path}})
                      : fail(QStringLiteral("project could not be opened"));
    }

    if (command == QLatin1StringView("new_project")) {
        if (!mayDiscard()) return fail(QStringLiteral("current project has unsaved changes"));
        if (exportInProgress()) return fail(QStringLiteral("an export is active"));
        exitComparison();
        ++m_projectGeneration;
        m_playback->closeMedia();
        m_project->replace(QStringLiteral("Untitled"), {}, {}, {});
        return api::ApiResponse::success();
    }

    if (command == QLatin1StringView("save_project")) {
        if (m_project->filePath().isEmpty())
            return fail(QStringLiteral("untitled project requires save_project_as"));
        return saveProjectTo(m_project->filePath(), false, false)
            ? api::ApiResponse::success() : fail(m_lastProjectSaveError);
    }

    if (command == QLatin1StringView("save_project_as")) {
        QString path;
        if (!localFile(QStringLiteral("path"), false, &path)
            || !path.endsWith(QStringLiteral(".atkproj"), Qt::CaseInsensitive))
            return fail(QStringLiteral("path must be an absolute local .atkproj destination"));
        return saveProjectTo(path, true, false)
            ? api::ApiResponse::success({{QStringLiteral("path"), path}})
            : fail(m_lastProjectSaveError);
    }

    if (command == QLatin1StringView("add_media")) {
        QStringList paths;
        if (params.value(QStringLiteral("path")).isString())
            paths.append(params.value(QStringLiteral("path")).toString());
        const QJsonArray requested = params.value(QStringLiteral("paths")).toArray();
        for (const QJsonValue& value : requested) if (value.isString()) paths.append(value.toString());
        if (paths.isEmpty()) return fail(QStringLiteral("path or paths is required"));
        QStringList validated;
        for (const QString& candidate : paths) {
            const QFileInfo info(candidate);
            if (!info.isAbsolute() || candidate.contains(QStringLiteral("://"))
                || !info.exists() || !info.isFile())
                return fail(QStringLiteral("every media path must be an absolute existing local file"));
            validated.append(info.absoluteFilePath());
        }
        addMediaFiles(validated, false);
        return api::ApiResponse::success({{QStringLiteral("added"), validated.size()}});
    }

    if (command == QLatin1StringView("list_sources")) {
        QJsonArray sources;
        for (int i = 0; i < m_project->entries().size(); ++i) {
            const auto& entry = m_project->entries().at(i);
            sources.append(QJsonObject{
                {QStringLiteral("id"), entry.id.toString(QUuid::WithoutBraces)},
                {QStringLiteral("index"), i},
                {QStringLiteral("path"), entry.source ? entry.source->filePath() : entry.storedPath},
                {QStringLiteral("displayName"), entry.displayName},
                {QStringLiteral("active"), i == m_project->activeIndex()},
                {QStringLiteral("availability"), sourceAvailability(entry.availability)},
            });
        }
        return api::ApiResponse::success({{QStringLiteral("sources"), sources}});
    }

    if (command == QLatin1StringView("activate_source")) {
        int index = -1;
        if (params.value(QStringLiteral("sourceId")).isString())
            index = m_project->indexForId(QUuid(params.value(QStringLiteral("sourceId")).toString()));
        else if (params.value(QStringLiteral("index")).isDouble())
            index = params.value(QStringLiteral("index")).toInt(-1);
        if (index < 0 || index >= m_project->entries().size())
            return fail(QStringLiteral("sourceId or index does not identify a source"));
        activatePlaylistIndex(index);
        return api::ApiResponse::success({{QStringLiteral("accepted"), true},
                                          {QStringLiteral("index"), index}});
    }

    if (command == QLatin1StringView("set_comparison_enabled")) {
        if (!params.value(QStringLiteral("enabled")).isBool())
            return fail(QStringLiteral("enabled must be a boolean"));
        if (params.value(QStringLiteral("enabled")).toBool()) enterComparison();
        else exitComparison();
        if (isComparisonActive() != params.value(QStringLiteral("enabled")).toBool())
            return fail(QStringLiteral("comparison requires two available sources"));
        return api::ApiResponse::success();
    }

    if (command == QLatin1StringView("load_compare_a")
        || command == QLatin1StringView("load_compare_b")) {
        int index = -1;
        if (params.value(QStringLiteral("sourceId")).isString())
            index = m_project->indexForId(QUuid(params.value(QStringLiteral("sourceId")).toString()));
        if (index < 0 && params.value(QStringLiteral("path")).isString()) {
            const QString target = QFileInfo(params.value(QStringLiteral("path")).toString()).absoluteFilePath();
            for (int i = 0; i < m_project->entries().size(); ++i) {
                const auto& entry = m_project->entries().at(i);
                if (entry.source && QFileInfo(entry.source->filePath()).absoluteFilePath() == target) {
                    index = i; break;
                }
            }
        }
        if (!sourceUsableForComparison(index)) return fail(QStringLiteral("comparison source is unavailable"));
        if (!isComparisonActive()) enterComparison();
        if (!isComparisonActive()) return fail(QStringLiteral("comparison could not be enabled"));
        const QUuid requestedId = m_project->entries().at(index).id;
        const bool accepted = command == QLatin1StringView("load_compare_a")
            ? selectComparisonSourceA(requestedId)
            : selectComparisonSourceB(requestedId);
        if (!accepted) return fail(QStringLiteral("comparison source conflicts with the other slot"));
        return api::ApiResponse::success();
    }

    if (command == QLatin1StringView("set_compare_offset")
        || command == QLatin1StringView("set_external_audio_offset")) {
        const bool hasFrames = params.value(QStringLiteral("frameOffset")).isDouble();
        const bool hasUs = params.value(QStringLiteral("offsetUs")).isDouble();
        if (hasFrames == hasUs) return fail(QStringLiteral("specify exactly one of frameOffset or offsetUs"));
        qint64 offsetUs = hasUs ? static_cast<qint64>(params.value(QStringLiteral("offsetUs")).toDouble())
                                : offsetUsForFrames(params.value(QStringLiteral("frameOffset")).toInt());
        if (command == QLatin1StringView("set_compare_offset")) {
            if (params.value(QStringLiteral("slot")).toString(QStringLiteral("b")) != QLatin1StringView("b"))
                return fail(QStringLiteral("only comparison slot b supports an offset"));
            m_compare->setSourceBOffsetUs(offsetUs);
            synchronizeComparison(m_playback->currentVideoFrame());
        } else {
            m_compare->setExternalAudioOffsetUs(offsetUs);
        }
        reanchorComparisonFollowers(true);
        refreshComparisonUi();
        return api::ApiResponse::success({{QStringLiteral("offsetUs"), static_cast<double>(offsetUs)}});
    }

    if (command == QLatin1StringView("set_compare_view")) {
        const QString mode = params.value(QStringLiteral("mode")).toString();
        const QHash<QString, playback::CompareLayout> modes{
            {QStringLiteral("side_by_side"), playback::CompareLayout::SideBySide},
            {QStringLiteral("stacked"), playback::CompareLayout::Stacked},
            {QStringLiteral("wipe"), playback::CompareLayout::Wipe},
            {QStringLiteral("blend"), playback::CompareLayout::Blend},
            {QStringLiteral("difference"), playback::CompareLayout::Difference},
        };
        if (!modes.contains(mode)) return fail(QStringLiteral("invalid comparison view mode"));
        m_compare->setLayout(modes.value(mode));
        if (params.value(QStringLiteral("wipePosition")).isDouble())
            m_compare->setWipePosition(qRound(params.value(QStringLiteral("wipePosition")).toDouble() * 100));
        if (params.value(QStringLiteral("blendAmount")).isDouble())
            m_compare->setBlendAmount(qRound(params.value(QStringLiteral("blendAmount")).toDouble() * 100));
        return api::ApiResponse::success();
    }

    if (command == QLatin1StringView("set_compare_audio_mode")) {
        const QString mode = params.value(QStringLiteral("mode")).toString();
        if (mode == QLatin1StringView("a")) applyComparisonAudioMode(playback::CompareAudioMode::SourceA);
        else if (mode == QLatin1StringView("b")) applyComparisonAudioMode(playback::CompareAudioMode::SourceB);
        else if (mode == QLatin1StringView("external")) applyComparisonAudioMode(playback::CompareAudioMode::External);
        else return fail(QStringLiteral("audio mode must be a, b, or external"));
        return api::ApiResponse::success();
    }

    if (command == QLatin1StringView("load_external_audio")) {
        QString path;
        if (!localFile(QStringLiteral("path"), true, &path))
            return fail(QStringLiteral("path must be an absolute existing local file"));
        QString validationError;
        if (!media::CompareAudioWorker::validateSource(path, &validationError))
            return fail(QStringLiteral("file has no usable audio stream"));
        m_compare->setExternalAudioPath(path);
        applyComparisonAudioMode(playback::CompareAudioMode::External);
        return api::ApiResponse::success({{QStringLiteral("path"), path}});
    }

    if (command == QLatin1StringView("clear_external_audio")) {
        clearExternalAudio();
        return api::ApiResponse::success();
    }

    if (command == QLatin1StringView("list_bookmarks")) {
        QJsonArray bookmarks;
        for (const auto& bookmark : m_timeline->bookmarks()) {
            bookmarks.append(QJsonObject{
                {QStringLiteral("id"), QString::number(bookmark.id)},
                {QStringLiteral("type"), bookmark.isRange() ? QStringLiteral("range") : QStringLiteral("point")},
                {QStringLiteral("start"), static_cast<double>(bookmark.frame)},
                {QStringLiteral("end"), static_cast<double>(bookmark.endFrame)},
                {QStringLiteral("displayStart"), static_cast<double>(bookmark.frame + 1)},
                {QStringLiteral("displayEnd"), static_cast<double>(bookmark.endFrame + 1)},
                {QStringLiteral("name"), bookmark.name},
                {QStringLiteral("note"), bookmark.note},
                {QStringLiteral("color"), bookmark.colorIndex},
            });
        }
        return api::ApiResponse::success({{QStringLiteral("bookmarks"), bookmarks}});
    }

    if (command == QLatin1StringView("export_review")) {
        if (exportInProgress()) return fail(QStringLiteral("an export is already active"));
        QString path;
        if (!localFile(QStringLiteral("path"), false, &path)
            || !path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
            return fail(QStringLiteral("path must be an absolute local .mp4 destination"));
        if (QFileInfo::exists(path) && !params.value(QStringLiteral("overwrite")).toBool(false))
            return fail(QStringLiteral("destination exists; set overwrite=true to replace it"));
        exporter::ExportSpec spec = exportSnapshot(path);
        QString burnInError;
        spec.burnIns = {}; // API contract: clean output unless burn-ins are requested
        if (!readBurnIns(&spec.burnIns, &burnInError)) return fail(burnInError);
        const QString validation = spec.validate();
        if (!validation.isEmpty()) return fail(validation);
        m_apiExportJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_apiExportState = QStringLiteral("running");
        m_apiExportOutputPath = path;
        m_apiExportError.clear();
        m_apiExportProgress = 0;
        m_apiExportFrame = 0;
        m_apiExportTotal = spec.frameCount();
        startExport(std::move(spec), false);
        return api::ApiResponse::success({{QStringLiteral("jobId"), m_apiExportJobId}});
    }

    if (command == QLatin1StringView("export_frame")) {
        if (exportInProgress()) return fail(QStringLiteral("an export is already active"));
        QString path;
        if (!localFile(QStringLiteral("path"), false, &path)
            || !path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            return fail(QStringLiteral("path must be an absolute local .png destination"));
        if (QFileInfo::exists(path) && !params.value(QStringLiteral("overwrite")).toBool(false))
            return fail(QStringLiteral("destination exists; set overwrite=true to replace it"));
        exporter::ExportSpec spec = exportSnapshot(path);
        spec.kind = exporter::ExportKind::CurrentFrame;
        QString burnInError;
        spec.burnIns = {}; // API contract: clean output unless burn-ins are requested
        if (!readBurnIns(&spec.burnIns, &burnInError)) return fail(burnInError);
        qint64 frame = m_timeline->currentFrame();
        if (params.contains(QStringLiteral("frame"))) {
            const double value = params.value(QStringLiteral("frame")).toDouble(-1.0);
            if (!params.value(QStringLiteral("frame")).isDouble() || !std::isfinite(value)
                || std::trunc(value) != value || value < 0
                || value >= static_cast<double>(std::numeric_limits<qint64>::max()))
                return fail(QStringLiteral("frame must be a zero-based integer"));
            frame = static_cast<qint64>(value);
        }
        spec.firstFrame = spec.lastFrame = frame;
        const QString validation = spec.validate();
        if (!validation.isEmpty()) return fail(validation);
        m_apiExportJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_apiExportState = QStringLiteral("running"); m_apiExportOutputPath = path;
        m_apiExportError.clear(); m_apiExportProgress = 0; m_apiExportFrame = 0;
        m_apiExportTotal = 1; startExport(std::move(spec), false);
        return api::ApiResponse::success({{QStringLiteral("jobId"), m_apiExportJobId}});
    }

    if (command == QLatin1StringView("export_image_sequence")) {
        if (exportInProgress()) return fail(QStringLiteral("an export is already active"));
        const QString rawDirectory = params.value(QStringLiteral("directory")).toString();
        const QFileInfo destination(rawDirectory);
        if (rawDirectory.isEmpty() || !destination.isAbsolute()
            || rawDirectory.contains(QStringLiteral("://")) || !destination.absoluteDir().exists())
            return fail(QStringLiteral("directory must be an absolute local destination with an existing parent"));
        if (destination.exists())
            return fail(QStringLiteral("image-sequence destination already exists"));
        exporter::ExportSpec spec = exportSnapshot(destination.absoluteFilePath());
        spec.kind = exporter::ExportKind::ImageSequence;
        QString burnInError;
        spec.burnIns = {}; // API contract: clean output unless burn-ins are requested
        if (!readBurnIns(&spec.burnIns, &burnInError)) return fail(burnInError);
        spec.imagePrefix = safeImagePrefix(params.value(QStringLiteral("prefix")).toString(
            exportBaseName(spec.sourceA)));
        const bool hasStart = params.contains(QStringLiteral("startFrame"));
        const bool hasEnd = params.contains(QStringLiteral("endFrame"));
        if (hasStart != hasEnd) return fail(QStringLiteral("startFrame and endFrame must be specified together"));
        if (hasStart) {
            const double start = params.value(QStringLiteral("startFrame")).toDouble(-1.0);
            const double end = params.value(QStringLiteral("endFrame")).toDouble(-1.0);
            if (!params.value(QStringLiteral("startFrame")).isDouble()
                || !params.value(QStringLiteral("endFrame")).isDouble()
                || !std::isfinite(start) || !std::isfinite(end)
                || std::trunc(start) != start || std::trunc(end) != end
                || start < 0 || end < start
                || end >= static_cast<double>(std::numeric_limits<qint64>::max()))
                return fail(QStringLiteral("startFrame and endFrame must be zero-based integers"));
            spec.firstFrame = static_cast<qint64>(start);
            spec.lastFrame = static_cast<qint64>(end);
        }
        const QString validation = spec.validate();
        if (!validation.isEmpty()) return fail(validation);
        m_apiExportJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_apiExportState = QStringLiteral("running"); m_apiExportOutputPath = spec.outputPath;
        m_apiExportError.clear(); m_apiExportProgress = 0; m_apiExportFrame = 0;
        m_apiExportTotal = spec.frameCount(); startExport(std::move(spec), false);
        return api::ApiResponse::success({{QStringLiteral("jobId"), m_apiExportJobId}});
    }

    if (command == QLatin1StringView("get_export_status")) {
        const QString requested = params.value(QStringLiteral("jobId")).toString();
        if (!requested.isEmpty() && requested != m_apiExportJobId)
            return fail(QStringLiteral("unknown export job"));
        return api::ApiResponse::success({
            {QStringLiteral("jobId"), m_apiExportJobId},
            {QStringLiteral("state"), m_apiExportState},
            {QStringLiteral("progress"), m_apiExportProgress / 100.0},
            {QStringLiteral("currentFrame"), static_cast<double>(m_apiExportFrame)},
            {QStringLiteral("totalFrames"), static_cast<double>(m_apiExportTotal)},
            {QStringLiteral("outputPath"), m_apiExportOutputPath},
            {QStringLiteral("error"), m_apiExportError},
        });
    }

    if (command == QLatin1StringView("cancel_export")) {
        if (!exportInProgress()) return fail(QStringLiteral("no export is active"));
        m_exportJob->cancel();
        return api::ApiResponse::success({{QStringLiteral("accepted"), true}});
    }

    if (command == QLatin1StringView("show_window")) {
        if (isMinimized()) showNormal(); else show();
        raise();
        activateWindow();
        return api::ApiResponse::success();
    }

    return fail(QStringLiteral("application command is not implemented: %1").arg(command));
}

void MainWindow::updateWindowTitle()
{
    const QString projectName = m_project && !m_project->filePath().isEmpty()
        ? QFileInfo(m_project->filePath()).fileName() : tr("Untitled");
    QString title = QStringLiteral("%1%2 — %3")
        .arg(m_project && m_project->isModified() ? QStringLiteral("*") : QString(),
             projectName, QString::fromLatin1(version::kApplicationName));

    setWindowTitle(title);
}

// --- Drawing on frames -------------------------------------------------------

void MainWindow::setAnnotateMode(bool enabled)
{
    QAction* toggle = m_commands->action(CommandId::ToggleAnnotateMode);
    if (enabled && isComparisonActive()) {
        if (toggle) { QSignalBlocker blocker(toggle); toggle->setChecked(false); }
        statusBar()->showMessage(tr("Drawing is not available during A/B Comparison."), 4000);
        return;
    }
    if (enabled && m_project->activeIndex() < 0) {
        if (toggle) { QSignalBlocker blocker(toggle); toggle->setChecked(false); }
        statusBar()->showMessage(tr("Open a clip to draw on its frames."), 4000);
        return;
    }
    if (toggle && toggle->isChecked() != enabled) {
        QSignalBlocker blocker(toggle);
        toggle->setChecked(enabled);
    }
    // Drawing is done on a held frame: a stroke always lands on the frame it
    // started on, so pausing avoids the picture moving under the pen.
    if (enabled && m_playback->isPlaying()) m_playback->pause();
    m_viewer->setAnnotateMode(enabled);
    m_commands->setDrawing(enabled);
    m_annotationToolbar->setVisible(enabled);
    if (enabled) m_viewer->setFocus(Qt::OtherFocusReason);
}

void MainWindow::bindAnnotationsToActiveSource()
{
    static const annotation::SourceAnnotations kNone;
    const int index = m_project->activeIndex();
    const bool valid = index >= 0 && index < m_project->entries().size();
    const QUuid id = valid ? m_project->entries().at(index).id : QUuid();
    const annotation::SourceAnnotations& sourceData = valid ? m_project->entries().at(index).annotations : kNone;
    if (id == m_annotationSourceId && m_annotations->annotations() == sourceData) return;
    // A marker selection belongs to the clip it was made on.
    if (id != m_annotationSourceId) m_timelineWidget->clearMarkerSelection();
    m_annotationSourceId = id;
    m_annotations->setAnnotations(sourceData);
    if (!valid && m_viewer->annotateMode()) setAnnotateMode(false);
    updateAnnotationActions();
}

qint64 MainWindow::annotationMediaTimeUs(qint64 frameIndex) const
{
    // The decoded timestamp of the frame actually drawn on. A stroke normally
    // ends on the frame it started on (drawing pauses playback); if not, the
    // index alone is authoritative and the time is left unknown.
    return frameIndex == m_displayedFrameIndex ? m_displayedFramePtsUs : 0;
}

void MainWindow::refreshNotesPanel()
{
    QVector<NoteRow> rows;
    for (const annotation::FrameAnnotation& frame : m_annotations->annotations().frames()) {
        const QString timecode = timeline::timecode::fromFrame(frame.frame, m_timeline->frameRate());
        for (const annotation::FrameNote& note : frame.notes)
            rows.append({frame.frame, timecode, note, frame.hasDrawings()});
    }
    m_notesPanel->setNotes(rows);
    m_notesPanel->setCurrentFrame(m_displayedFrameIndex,
                                  timeline::timecode::fromFrame(std::max<qint64>(0, m_displayedFrameIndex),
                                                                m_timeline->frameRate()),
                                  !m_annotationSourceId.isNull() && m_displayedFrameIndex >= 0);
}

void MainWindow::refreshViewerAnnotations()
{
    annotation::FrameAnnotation shown = m_displayedFrameIndex >= 0 ? m_annotations->frame(m_displayedFrameIndex)
                                                                   : annotation::FrameAnnotation{};
    // With holding on, a frame without drawings of its own shows the latest
    // earlier drawing. The viewer gets it as this frame's, so it paints,
    // selects and erases it as usual; annotationEditFrame() sends the edits
    // back to the frame it was drawn on. Notes stay with their own frame.
    m_heldSourceFrame = -1;
    if (m_holdMode != annotation::HoldMode::Off && m_displayedFrameIndex >= 0 && !shown.hasDrawings()) {
        const qint64 held = annotation::heldDrawingFrame(m_annotations->annotations(), m_displayedFrameIndex);
        if (held >= 0 && held != m_displayedFrameIndex) {
            m_heldSourceFrame = held;
            shown.items = m_annotations->frame(held).items;
        }
    }
    m_viewer->setAnnotationFrame(shown);
    m_viewer->setGhostFrames(m_displayedFrameIndex,
                             annotation::ghostFramesFor(m_annotations->annotations(), m_displayedFrameIndex,
                                                        m_ghostSettings));
}

qint64 MainWindow::annotationEditFrame(qint64 frameIndex, bool newDrawing) const
{
    if (m_heldSourceFrame < 0 || frameIndex != m_displayedFrameIndex) return frameIndex;
    if (newDrawing && m_holdMode == annotation::HoldMode::AddNew) return frameIndex;
    return m_heldSourceFrame;
}

void MainWindow::applyHoldMode(annotation::HoldMode mode, bool announce)
{
    m_holdMode = mode;
    if (mode != annotation::HoldMode::Off) m_holdOnMode = mode;
    const auto check = [this](CommandId id, bool on) {
        if (QAction* action = m_commands->action(id)) {
            QSignalBlocker blocker(action);
            action->setChecked(on);
        }
    };
    check(CommandId::ToggleHoldDrawings, mode != annotation::HoldMode::Off);
    check(CommandId::HoldDrawingsAddToCurrent, mode == annotation::HoldMode::AddToCurrent);
    check(CommandId::HoldDrawingsAddNew, mode == annotation::HoldMode::AddNew);
    check(CommandId::HoldDrawingsOff, mode == annotation::HoldMode::Off);
    m_settings->setHoldDrawings(mode);
    refreshViewerAnnotations();
    refreshHoldSpans();
    if (!announce) return;
    switch (mode) {
    case annotation::HoldMode::Off:
        statusBar()->showMessage(tr("Drawings show on their own frame only"), 2500);
        break;
    case annotation::HoldMode::AddToCurrent:
        statusBar()->showMessage(tr("Holding drawings: drawing on a held frame adds to the held drawing"), 2500);
        break;
    case annotation::HoldMode::AddNew:
        statusBar()->showMessage(tr("Holding drawings: drawing on a held frame starts a new drawing there"), 2500);
        break;
    }
}

void MainWindow::refreshHoldSpans()
{
    m_timelineWidget->setHoldSpans(
        m_holdMode == annotation::HoldMode::Off || m_annotationSourceId.isNull()
            ? QVector<annotation::HoldSpan>{}
            : annotation::holdSpans(m_annotations->annotations(), std::max<qint64>(0, m_timeline->frameCount() - 1)));
}

void MainWindow::refreshPlaybackSpeed()
{
    m_transport->setPlaybackSpeed(m_playback->playbackSpeed(),
                                  m_playback->hasMedia() ? m_timeline->frameRate() : media::FrameRate{});
}

void MainWindow::zoomViewerToPercent(qreal percent)
{
    if (isComparisonActive() && m_compare->layout() != playback::CompareLayout::SideBySide
        && m_compare->layout() != playback::CompareLayout::Stacked) m_compareComposite->zoomToPercent(percent);
    else activeViewer()->zoomToPercent(percent);
}

void MainWindow::applyTracingPaper(int percent)
{
    m_tracingPaper = std::clamp(percent, 0, 100);
    if (m_tracingPaper > 0) m_tracingPaperLevel = m_tracingPaper;
    m_viewer->setTracingPaper(m_tracingPaper);
    m_annotationToolbar->setTracingPaper(m_tracingPaper, m_tracingPaperLevel);
    if (QAction* action = m_commands->action(CommandId::ToggleTracingPaper)) {
        QSignalBlocker blocker(action);
        action->setChecked(m_tracingPaper > 0);
    }
    m_settings->setTracingPaper(m_tracingPaper);
    m_settings->setTracingPaperLevel(m_tracingPaperLevel);
}

void MainWindow::applyGhostSettings(const annotation::GhostSettings& settings)
{
    m_ghostSettings = settings;
    m_ghostSettings.before = std::clamp(m_ghostSettings.before, 0, annotation::GhostSettings::kMaxRange);
    m_ghostSettings.after = std::clamp(m_ghostSettings.after, 0, annotation::GhostSettings::kMaxRange);
    m_annotationToolbar->setGhostSettings(m_ghostSettings);
    if (QAction* action = m_commands->action(CommandId::ToggleGhosting)) {
        QSignalBlocker blocker(action);
        action->setChecked(m_ghostSettings.enabled);
    }
    m_settings->setGhostSettings(m_ghostSettings);
    refreshViewerAnnotations();
}

void MainWindow::applyAnnotationTool(const annotation::AnnotationToolSettings& settings)
{
    m_viewer->setAnnotationTool(settings);
    m_settings->setAnnotationTool(settings);
}

void MainWindow::seekAnnotatedFrame(int direction)
{
    const QVector<int64_t> frames = m_annotations->annotatedFrames();
    if (frames.isEmpty()) {
        statusBar()->showMessage(tr("No drawings on this clip."), 2500);
        return;
    }
    const int64_t current = m_displayedFrameIndex >= 0 ? m_displayedFrameIndex : m_playback->currentFrame();
    int64_t target = -1;
    if (direction > 0) {
        const auto it = std::upper_bound(frames.cbegin(), frames.cend(), current);
        target = it != frames.cend() ? *it : frames.constFirst(); // wrap, like bookmarks
    } else {
        const auto it = std::lower_bound(frames.cbegin(), frames.cend(), current);
        target = it != frames.cbegin() ? *(it - 1) : frames.constLast();
    }
    m_playback->seekFrame(target);
}

void MainWindow::importBrushSet()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Photoshop Brushes"), m_lastMediaDirectory,
                                                      tr("Photoshop Brushes (*.abr);;All Files (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 512LL * 1024 * 1024) {
        QMessageBox::warning(this, tr("Import Brushes"), tr("Could not read %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    const QString setName = QFileInfo(path).completeBaseName();
    const annotation::AbrReadResult result = annotation::readAbr(file.readAll(), setName);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("Import Brushes"), result.error);
        return;
    }
    annotation::BrushSet set;
    set.name = setName;
    set.tips = result.tips;
    QString error;
    if (!m_brushLibrary->addSet(set, &error)) {
        QMessageBox::warning(this, tr("Import Brushes"), tr("The brushes could not be saved: %1").arg(error));
        return;
    }
    m_brushLibrary->registerAll();
    m_annotationToolbar->setBrushSets(m_brushLibrary->sets());
    // Ready to paint with the first imported brush.
    auto settings = m_annotationToolbar->settings();
    settings.tool = annotation::AnnotationTool::Stamp;
    settings.brushTipId = set.tips.constFirst().id;
    m_annotationToolbar->setSettings(settings);
    applyAnnotationTool(settings);
    QString message = tr("Imported %n brush(es) from %1.", nullptr, int(set.tips.size())).arg(setName);
    if (result.skippedComputed > 0)
        message += QLatin1Char(' ') + tr("%n round brush(es) defined only by settings were skipped.", nullptr,
                                          result.skippedComputed);
    statusBar()->showMessage(message, 6000);
}

void MainWindow::removeBrushSet(const QString& name)
{
    if (QMessageBox::question(this, tr("Remove Brush Set"),
                              tr("Remove the brush set \"%1\"? Drawings already made with it keep their look "
                                 "in projects that were saved with it.").arg(name))
        != QMessageBox::Yes)
        return;
    m_brushLibrary->removeSet(name);
    m_annotationToolbar->setBrushSets(m_brushLibrary->sets());
}

void MainWindow::updateAnnotationActions()
{
    if (!m_commands || !m_annotations) return;
    if (QAction* undo = m_commands->action(CommandId::UndoAnnotation)) undo->setEnabled(m_annotations->canUndo());
    if (QAction* redo = m_commands->action(CommandId::RedoAnnotation)) redo->setEnabled(m_annotations->canRedo());
    if (QAction* paste = m_commands->action(CommandId::PasteAnnotations))
        paste->setEnabled(!m_annotationClipboard.isEmpty());
}

namespace {

/// "2 drawings and 1 note", "1 drawing", "3 notes".
QString describeContent(qsizetype drawings, qsizetype notes)
{
    const QString drawingText = drawings == 1 ? MainWindow::tr("1 drawing")
                                              : MainWindow::tr("%1 drawings").arg(drawings);
    const QString noteText = notes == 1 ? MainWindow::tr("1 note") : MainWindow::tr("%1 notes").arg(notes);
    if (drawings > 0 && notes > 0) return MainWindow::tr("%1 and %2").arg(drawingText, noteText);
    return drawings > 0 ? drawingText : noteText;
}

} // namespace

void MainWindow::copyAnnotations(bool cut)
{
    if (m_annotationSourceId.isNull() || m_displayedFrameIndex < 0) return;
    // What is on screen: a held drawing counts as this frame's.
    annotation::FrameAnnotation frame = m_annotations->frame(m_displayedFrameIndex);
    const qint64 drawingFrame = annotationEditFrame(m_displayedFrameIndex, false);
    if (drawingFrame != m_displayedFrameIndex) frame.items = m_annotations->frame(drawingFrame).items;
    annotation::FrameAnnotation copied;
    copied.frame = frame.frame;
    copied.mediaTimeUs = frame.mediaTimeUs;
    // With a drawing picked by the Select tool, only that drawing; otherwise
    // the whole frame, drawings and notes.
    const quint64 selected = m_viewer->annotateMode() ? m_viewer->selectedItemId() : 0;
    if (selected != 0) {
        for (const annotation::AnnotationItem& item : frame.items)
            if (item.id == selected) copied.items.append(item);
    }
    if (copied.items.isEmpty()) {
        copied.items = frame.items;
        copied.notes = frame.notes;
    }
    if (copied.isEmpty()) {
        statusBar()->showMessage(tr("Nothing to %1 on this frame.").arg(cut ? tr("cut") : tr("copy")), 3000);
        return;
    }
    m_annotationClipboard = copied;
    updateAnnotationActions();
    const QString what = describeContent(copied.items.size(), copied.notes.size());
    if (cut) {
        QVector<quint64> itemIds;
        QVector<quint64> noteIds;
        for (const annotation::AnnotationItem& item : copied.items) itemIds.append(item.id);
        for (const annotation::FrameNote& note : copied.notes) noteIds.append(note.id);
        m_viewer->clearSelection();
        if (drawingFrame == m_displayedFrameIndex) {
            m_annotations->removeContent(m_displayedFrameIndex, itemIds, noteIds);
        } else {
            // A held drawing is cut from the frame it was drawn on; one undo.
            m_annotations->beginEditGroup();
            if (!itemIds.isEmpty()) m_annotations->removeContent(drawingFrame, itemIds, {});
            if (!noteIds.isEmpty()) m_annotations->removeContent(m_displayedFrameIndex, {}, noteIds);
            m_annotations->endEditGroup();
        }
        statusBar()->showMessage(tr("Cut %1. Go to another frame and paste (Ctrl+V).").arg(what), 4000);
    } else {
        statusBar()->showMessage(tr("Copied %1. Go to another frame and paste (Ctrl+V).").arg(what), 4000);
    }
}

void MainWindow::copyNote(qint64 frame, quint64 noteId, bool cut)
{
    if (m_annotationSourceId.isNull()) return;
    const annotation::FrameAnnotation content = m_annotations->frame(frame);
    for (const annotation::FrameNote& note : content.notes) {
        if (note.id != noteId) continue;
        m_annotationClipboard = annotation::FrameAnnotation{};
        m_annotationClipboard.frame = frame;
        m_annotationClipboard.notes.append(note);
        updateAnnotationActions();
        if (cut) m_annotations->removeNote(frame, noteId);
        statusBar()->showMessage(cut ? tr("Cut the note. Go to another frame and paste (Ctrl+V).")
                                     : tr("Copied the note. Go to another frame and paste (Ctrl+V)."),
                                 4000);
        return;
    }
}

void MainWindow::pasteAnnotations()
{
    if (m_annotationSourceId.isNull() || m_displayedFrameIndex < 0) return;
    if (m_annotationClipboard.isEmpty()) {
        statusBar()->showMessage(tr("Nothing to paste. Copy a drawing, a note or a frame first."), 3000);
        return;
    }
    const QVector<quint64> ids = m_annotations->pasteContent(m_displayedFrameIndex,
                                                             annotationMediaTimeUs(m_displayedFrameIndex),
                                                             m_annotationClipboard.items,
                                                             m_annotationClipboard.notes);
    if (ids.isEmpty()) {
        statusBar()->showMessage(tr("The copied drawings and notes could not be pasted (frame limit reached)."),
                                 4000);
        return;
    }
    // A single pasted drawing comes up selected, so it can be dragged into
    // place straight away with the Select tool.
    if (m_annotationClipboard.items.size() == 1 && m_viewer->annotateMode()
        && m_viewer->annotationTool().tool == annotation::AnnotationTool::Select)
        m_viewer->selectItem(ids.first());
    statusBar()->showMessage(tr("Pasted %1 onto frame %2.")
                                 .arg(describeContent(m_annotationClipboard.items.size(),
                                                      m_annotationClipboard.notes.size()))
                                 .arg(m_displayedFrameIndex + 1),
                             3000);
}

void MainWindow::moveAnnotatedFrame(qint64 from, qint64 to, bool copy)
{
    if (m_annotationSourceId.isNull()) return;
    const annotation::FrameAnnotation content = m_annotations->frame(from);
    const QString what = describeContent(content.items.size(), content.notes.size());
    if (!m_annotations->moveFrameContent(from, to, annotationMediaTimeUs(to), copy)) {
        statusBar()->showMessage(tr("Frame %1's notes and drawings could not be %2 to frame %3 (frame limit reached).")
                                     .arg(from + 1).arg(copy ? tr("copied") : tr("moved")).arg(to + 1),
                                 4000);
        return;
    }
    statusBar()->showMessage((copy ? tr("Copied %1 from frame %2 to frame %3.") : tr("Moved %1 from frame %2 to frame %3."))
                                 .arg(what).arg(from + 1).arg(to + 1),
                             3000);
    // Show where they landed.
    if (to != m_displayedFrameIndex) m_playback->seekFrame(to);
}

void MainWindow::moveAnnotatedFrames(const QVector<qint64>& frames, qint64 offset, bool copy)
{
    if (m_annotationSourceId.isNull() || frames.isEmpty() || offset == 0) return;
    QVector<int64_t> sources(frames.cbegin(), frames.cend());
    const auto mediaTime = [this](int64_t frame) { return annotationMediaTimeUs(frame); };
    const QString shift = (offset > 0 ? QStringLiteral("+") : QString()) + QString::number(offset);
    if (!m_annotations->moveFramesContent(sources, offset, mediaTime, copy)) {
        m_timelineWidget->revertGroupDrop();
        statusBar()->showMessage(tr("The selected notes and drawings could not be %1 %2 frames (frame limit reached).")
                                     .arg(copy ? tr("copied") : tr("moved"), shift),
                                 4000);
        return;
    }
    const QString what = frames.size() == 1 ? tr("1 frame's notes and drawings")
                                            : tr("%1 frames' notes and drawings").arg(frames.size());
    statusBar()->showMessage((copy ? tr("Copied %1 by %2 frames.") : tr("Moved %1 by %2 frames.")).arg(what, shift),
                             3000);
    // If the frame on screen was one of them, follow it.
    if (!copy && frames.contains(m_displayedFrameIndex)) m_playback->seekFrame(m_displayedFrameIndex + offset);
}

} // namespace atk::ui
