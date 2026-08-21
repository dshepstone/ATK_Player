#include "ui/MainWindow.h"

#include "api/ApiServer.h"
#include "playback/CompareSession.h"
#include "core/Logging.h"
#include "core/Version.h"
#include "media/MediaSource.h"
#include "media/PlaylistProbeWorker.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "timeline/TimelineModel.h"
#include "ui/ApplicationSettings.h"
#include "ui/BookmarkPanel.h"
#include "ui/PreferencesDialog.h"
#include "ui/Resources.h"
#include "ui/SourcesPanel.h"
#include "ui/StatusInfoBar.h"
#include "ui/Theme.h"
#include "ui/TimelineWidget.h"
#include "ui/TimelineRangeSlider.h"
#include "ui/TransportControls.h"
#include "ui/ViewerWidget.h"
#include "ui/commands/CommandRegistry.h"

#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QWidgetAction>
#include <QThread>

#include <QAction>
#include <QCloseEvent>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>

namespace atk::ui {
namespace {

using commands::CommandId;

/// Commands that get a separator drawn above them, to group the menus without
/// hard-coding the menu structure a second time.
bool startsNewGroup(CommandId id)
{
    switch (id) {
    case CommandId::CloseSource:
    case CommandId::Quit:
    case CommandId::PreviousFrame:
    case CommandId::ToggleLoop:
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

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(QString(), parent)
{
}

MainWindow::MainWindow(const QString& settingsIniPath, QWidget* parent)
    : QMainWindow(parent)
{
    ensureResourcesInitialized();
    if (!settingsIniPath.isEmpty())
        m_settings = std::make_unique<ApplicationSettings>(settingsIniPath);
    setObjectName(QStringLiteral("AtkMainWindow"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/ATK_Player_Icon.png")));

    buildModels();
    buildWidgets();
    buildMenus();
    buildAudioControls();
    connectSignals();

    updateWindowTitle();
    resize(1280, 800);
    restoreApplicationLayout();

    qCInfo(log::ui) << "Main window constructed";
}

MainWindow::~MainWindow()
{
    saveApplicationLayout();
    if (m_probeThread) {
        m_probeThread->quit();
        m_probeThread->wait();
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
                const int64_t frameCount = metadata.effectiveFrameCount();
                if (frameCount <= 0 || !m_project->relinkSource(id, replacement, frameCount)) return;
                if (index == m_project->activeIndex()) {
                    m_project->setActiveIndex(-1);
                    activatePlaylistIndex(index, false);
                }
                return;
            }
            m_project->applyProbeResult(id, path, token, metadata, error, missing);
        });
    m_probeThread->start();
    m_compare  = std::make_unique<playback::CompareSession>();
    m_apiServer = std::make_unique<api::ApiServer>(m_playback.get(), m_timeline.get());

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
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_viewer = new ViewerWidget(central);
    m_viewer->setPlaceholderText(tr("ATK PLAYER"));
    m_viewer->setPlaceholderSubtext(
        tr("No media loaded — video playback arrives in milestone M1"));
    column->addWidget(m_viewer, 1);

    m_timelineWidget = new TimelineWidget(central);
    m_timelineWidget->setModel(m_timeline.get());
    column->addWidget(m_timelineWidget);
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
        field->setFixedWidth(72);
        field->setAlignment(Qt::AlignCenter);
    }
    auto* reviewRangeRow = new QHBoxLayout;
    reviewRangeRow->setContentsMargins(8, 0, 8, 0);
    reviewRangeRow->setSpacing(6);
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
    column->addWidget(m_transport);

    setCentralWidget(central);

    // --- Left dock: sources ----------------------------------------------
    m_sources = new SourcesPanel(this);
    m_sources->setProject(m_project.get());

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

    m_commands->action(CommandId::OpenMedia)->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_commands->action(CommandId::Preferences)->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_commands->action(CommandId::AddBookmark)->setIcon(QIcon(QStringLiteral(":/icons/bookmark-add.svg")));
    m_commands->action(CommandId::TimelineZoomFit)->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
    m_commands->action(CommandId::ZoomFit)->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_commands->action(CommandId::ZoomActualSize)->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));

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
        bookmarksAction->setChecked(true);
    }
    if (QAction* snapAction = m_commands->action(CommandId::ToggleBookmarkSnap)) {
        snapAction->setChecked(m_settings->bookmarkSnapEnabled());
        m_timelineWidget->setBookmarkSnapEnabled(m_settings->bookmarkSnapEnabled());
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
}

void MainWindow::connectSignals()
{
    connect(m_commands, &CommandRegistry::commandTriggered,
            this, &MainWindow::onCommand);

    connect(m_viewer, &ViewerWidget::zoomChanged, this,
            [this](qreal percent, bool fitMode) {
        const QString value = tr("%1%").arg(qRound(percent));
        m_viewerZoomStatus->setText(fitMode ? tr("Fit %1").arg(value) : value);
    });
    m_viewer->resetNavigationToFit();

    connect(m_playback.get(), &playback::PlaybackController::stateChanged,
            this, &MainWindow::onPlayerStateChanged);
    connect(m_sources, &SourcesPanel::addMediaRequested, this, &MainWindow::addMediaDialog);
    connect(m_sources, &SourcesPanel::removeRequested, this, &MainWindow::removePlaylistIndex);
    connect(m_sources, &SourcesPanel::moveRequested, this, &MainWindow::movePlaylistIndex);
    connect(m_sources, &SourcesPanel::relinkRequested, this, [this](int) { relinkSelectedMedia(); });
    connect(m_sources, &SourcesPanel::sourceActivated, this,
            [this](int index) { activatePlaylistIndex(index); });

    // Decoded frames reach the viewer through the controller, so the viewer
    // never talks to the decoder and never decodes inside a paint event.
    connect(m_playback.get(), &playback::PlaybackController::frameChanged,
            this, [this](const media::VideoFrame& frame) { m_viewer->setFrame(frame); });

    connect(m_playback.get(), &playback::PlaybackController::mediaOpened,
            this, &MainWindow::onMediaOpened);

    // The waveform lives in the controller and is repainted in place; only a
    // repaint request crosses to the widget, never a copy of the peaks.
    connect(m_playback.get(), &playback::PlaybackController::waveformChanged,
            this, [this] { m_timelineWidget->refreshWaveform(); });

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
                m_viewer->setEmpty();
                m_viewer->setSourceAspectRatio(0.0);
                m_sources->clearCurrentMedia();
                m_statusInfo->clearMediaInfo();
                installPlaceholderTimeline();
                updateTransportEnabled();
                updateWindowTitle();
            });

    connect(m_playback.get(), &playback::PlaybackController::loopEnabledChanged,
            this, [this](bool enabled) {
                if (QAction* action = m_commands->action(CommandId::ToggleLoop)) {
                    QSignalBlocker blocker(action);
                    action->setChecked(enabled);
                }
            });

    // Scrubbing the timeline goes through the controller, not straight into the
    // model, so a drag behaves exactly like an API seek.
    connect(m_timelineWidget, &TimelineWidget::scrubStarted,
            m_playback.get(), &playback::PlaybackController::beginScrub);
    connect(m_timelineWidget, &TimelineWidget::scrubPreviewRequested,
            m_playback.get(), &playback::PlaybackController::scrubToFrame);
    connect(m_timelineWidget, &TimelineWidget::scrubFinished,
            m_playback.get(), &playback::PlaybackController::endScrub);

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
    connect(m_timeline.get(), &timeline::TimelineModel::bookmarksChanged, this, [this] {
        if (m_restoringSourceState || m_project->activeIndex() < 0) return;
        m_project->mutableEntries()[m_project->activeIndex()].bookmarks = m_timeline->bookmarks();
        m_project->setModified(true);
    });
    connect(m_timeline.get(), &timeline::TimelineModel::viewportChanged, this,
            [this](qint64 start, qint64 end) {
                if (m_restoringSourceState || m_project->activeIndex() < 0) return;
                m_project->mutableEntries()[m_project->activeIndex()].playbackRange = {start, end, true};
                m_project->setModified(true);
            });

    // The frame count only becomes final once media is open, and the transport
    // depends on whether there is an extent at all.
    connect(m_timeline.get(), &timeline::TimelineModel::frameCountChanged,
            this, [this](qint64 count) {
                updateTransportEnabled();
                const int maximum = static_cast<int>(std::max<qint64>(1, count));
                m_reviewStartFrame->setRange(1, maximum);
                m_reviewEndFrame->setRange(1, maximum);
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
        m_playback->setLoopEnabled(checked);
        return;
    case CommandId::SkipBack10Seconds:
        m_playback->skipBySeconds(-10); return;
    case CommandId::SkipForward10Seconds:
        m_playback->skipBySeconds(10); return;
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
        statusBar()->showMessage(tr("Range in set to frame %1").arg(m_timeline->currentFrame()), 2000);
        return;
    case CommandId::SetRangeOut:
        m_timeline->setRangeOutAtCurrentFrame();
        statusBar()->showMessage(tr("Range out set to frame %1").arg(m_timeline->currentFrame()), 2000);
        return;
    case CommandId::ClearRange:
        m_timeline->clearPlaybackRange();
        statusBar()->showMessage(tr("Playback range cleared"), 2000);
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

    // --- View: fully wired ------------------------------------------------
    case CommandId::ZoomFit:
        m_viewer->fitImage();
        return;
    case CommandId::ZoomActualSize:
        m_viewer->showActualSize();
        return;
    case CommandId::ZoomIn:
        m_viewer->zoomIn();
        return;
    case CommandId::ZoomOut:
        m_viewer->zoomOut();
        return;
    case CommandId::ToggleFullScreen:
        if (checked) {
            showFullScreen();
        } else {
            showNormal();
        }
        return;
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
    case CommandId::About:
        QMessageBox::about(
            this,
            tr("About ATK Player"),
            tr("<h3>%1 %2</h3>"
               "<p><b>Animation Review Player</b></p>"
               "<p>Frame-accurate playback, synchronized audio, timeline review "
               "ranges, bookmarks and animator-focused navigation.</p>"
               "<p>This is an independent open-source development build.</p>")
                .arg(QString::fromLatin1(version::kApplicationName),
                     QString::fromLatin1(version::kString)));
        return;

    // --- Declared, not yet implemented ------------------------------------
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
    if (dialog.exec() == QDialog::Accepted) applyPreferences(dialog);
}

void MainWindow::applyPreferences(const PreferencesDialog& dialog)
{
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
    const QByteArray state = m_settings->windowState();
    if (!state.isEmpty() && !restoreState(state)) {
        addDockWidget(Qt::LeftDockWidgetArea, m_sourcesDock);
        addDockWidget(Qt::RightDockWidgetArea, m_bookmarksDock);
    }
}

void MainWindow::saveApplicationLayout()
{
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
        m_playlistPlaybackActive = true;
        statusBar()->showMessage(tr("Playing"), 1500);
        break;
    case PlayerState::Paused:
        m_playlistPlaybackActive = false;
        statusBar()->showMessage(tr("Paused"), 1500);
        break;
    case PlayerState::Ended:
        if (m_playlistPlaybackActive && !m_playback->isLoopEnabled()
            && m_project->activeIndex() + 1 < m_project->entries().size()) {
            activatePlaylistIndex(m_project->activeIndex() + 1, true);
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
    const QString filter = tr(
        "Video Files (*.mp4 *.mov *.avi *.mkv *.m4v *.webm *.mpg *.mpeg *.wmv *.flv);;"
        "All Files (*.*)");

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Media"), m_lastMediaDirectory, filter);

    if (path.isEmpty()) {
        return;
    }

    m_lastMediaDirectory = QFileInfo(path).absolutePath();
    openMediaFile(path);
}

void MainWindow::openMediaFile(const QString& filePath)
{
    if (!confirmDiscardChanges()) return;
    ++m_projectGeneration;
    m_restoringSourceState = true;
    m_project->clear();
    m_project->setName(QStringLiteral("Untitled"));
    m_project->setFilePath(QString());
    m_restoringSourceState = false;
    addMediaFiles({filePath});
}

void MainWindow::onMediaOpened(const media::MediaMetadata& metadata)
{
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

    updateTransportEnabled();
    updateWindowTitle();

    statusBar()->showMessage(tr("Opened %1").arg(metadata.fileName), 3000);
    if (m_playAfterSourceOpen) {
        m_playAfterSourceOpen = false;
        m_playback->play();
    }
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
    const QString filter = tr("Video Files (*.mp4 *.mov *.avi *.mkv *.m4v *.webm *.mpg *.mpeg *.wmv *.flv);;All Files (*.*)");
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Media to Playlist"), m_lastMediaDirectory, filter);
    if (!paths.isEmpty()) { m_lastMediaDirectory = QFileInfo(paths.first()).absolutePath(); addMediaFiles(paths); }
}

void MainWindow::addMediaFiles(const QStringList& paths)
{
    const bool wasEmpty = m_project->entries().isEmpty();
    for (const QString& path : paths) {
        if (path.isEmpty()) continue;
        m_project->addSource(std::make_shared<media::MediaSource>(QFileInfo(path).absoluteFilePath()));
    }
    if (wasEmpty && !m_project->entries().isEmpty()) {
        m_project->setActiveIndex(-1);
        activatePlaylistIndex(0);
    }
    startPlaylistProbes();
    updateWindowTitle();
}

void MainWindow::startProbe(const QUuid& id, const QString& path)
{
    const quint64 token = m_nextProbeToken++;
    if (!m_project->beginProbe(id, path, token)) return;
    QMetaObject::invokeMethod(m_probeWorker, [worker = m_probeWorker, id, path, token] {
        worker->probe(id, path, token);
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
    entry.playbackRange = {m_timeline->viewport().startFrame(), m_timeline->viewport().endFrame(), true};
}

void MainWindow::restoreActiveReviewState()
{
    if (m_project->activeIndex() < 0) return;
    const auto& entry = m_project->entries().at(m_project->activeIndex());
    m_restoringSourceState = true;
    m_timeline->clearBookmarks();
    for (const auto& bookmark : entry.bookmarks) m_timeline->addBookmark(bookmark);
    if (entry.playbackRange.enabled && entry.playbackRange.isValid())
        m_playback->activateReviewRange(entry.playbackRange.startFrame, entry.playbackRange.endFrame);
    else
        m_timeline->fitViewport();
    m_viewer->resetNavigationToFit();
    m_restoringSourceState = false;
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
    m_playback->openMedia(entry.source->filePath());
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

bool MainWindow::confirmDiscardChanges()
{
    if (!m_project->isModified()) return true;
    const auto choice = QMessageBox::warning(this, tr("Unsaved Project"),
        tr("Save changes to the current project?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) return saveProject();
    return true;
}

void MainWindow::newProject()
{
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
    project::Project loaded;
    const auto result = project::ProjectSerializer::load(loaded, path);
    if (!result.ok) {
        if (!m_suppressProjectOpenError) QMessageBox::critical(this, tr("Open Project"), result.errorMessage);
        return false;
    }
    ++m_projectGeneration;
    m_playback->closeMedia();
    m_project->replace(loaded.name(), loaded.filePath(), loaded.entries(), loaded.currentSourceId());
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
    startPlaylistProbes();
    updateWindowTitle(); return true;
}

bool MainWindow::saveProjectTo(const QString& path)
{
    saveActiveReviewState();
    const auto result = project::ProjectSerializer::save(*m_project, path);
    if (!result.ok) { QMessageBox::critical(this, tr("Save Project"), result.errorMessage); return false; }
    m_project->setFilePath(QFileInfo(path).absoluteFilePath());
    m_project->setName(QFileInfo(path).completeBaseName());
    m_project->setModified(false);
    m_settings->addRecentProject(path); refreshRecentProjectsMenu();
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

void MainWindow::reopenLastProjectIfEnabled()
{
    if (!m_settings->reopenLastProject()) return;
    const QString path = m_settings->lastProjectPath();
    m_suppressProjectOpenError = true;
    const bool opened = !path.isEmpty() && QFileInfo::exists(path) && openProjectFile(path);
    m_suppressProjectOpenError = false;
    if (!opened) {
        m_settings->setLastProjectPath(QString());
        statusBar()->showMessage(tr("The last project could not be reopened."), 5000);
    }
}

void MainWindow::relinkSelectedMedia()
{
    const int index = m_sources->selectedIndex();
    if (index < 0 || index >= m_project->entries().size()) return;
    const auto id = m_project->entries().at(index).id;
    const QString path = QFileDialog::getOpenFileName(this, tr("Relink Media"), QString(),
        tr("Video Files (*.mp4 *.mov *.avi *.mkv *.m4v *.webm *.mpg *.mpeg *.wmv *.flv);;All Files (*.*)"));
    const QFileInfo info(path);
    if (path.isEmpty()) return;
    if (!info.exists() || !info.isFile()) { QMessageBox::critical(this, tr("Relink Media"), tr("The selected media file does not exist.")); return; }
    m_pendingRelinkId = id;
    m_pendingRelinkPath = info.absoluteFilePath();
    m_pendingRelinkToken = m_nextProbeToken++;
    m_pendingRelinkProjectGeneration = m_projectGeneration;
    QMetaObject::invokeMethod(m_probeWorker,
        [worker = m_probeWorker, id, path = m_pendingRelinkPath, token = m_pendingRelinkToken] {
            worker->probe(id, path, token);
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
    return saveProjectTo(path);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscardChanges()) { saveApplicationLayout(); event->accept(); }
    else event->ignore();
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

} // namespace atk::ui
