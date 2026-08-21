#include "ui/MainWindow.h"

#include "api/ApiServer.h"
#include "playback/CompareSession.h"
#include "core/Logging.h"
#include "core/Version.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "timeline/TimelineModel.h"
#include "ui/SourcesPanel.h"
#include "ui/StatusInfoBar.h"
#include "ui/Theme.h"
#include "ui/TimelineWidget.h"
#include "ui/TimelineRangeSlider.h"
#include "ui/TransportControls.h"
#include "ui/ViewerWidget.h"
#include "ui/commands/CommandRegistry.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QWidgetAction>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
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
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("AtkMainWindow"));

    buildModels();
    buildWidgets();
    buildMenus();
    buildAudioControls();
    connectSignals();

    updateWindowTitle();
    resize(1280, 800);

    qCInfo(log::ui) << "Main window constructed";
}

MainWindow::~MainWindow() = default;

void MainWindow::buildModels()
{
    m_timeline = std::make_unique<timeline::TimelineModel>();
    m_playback = std::make_unique<playback::PlaybackController>(m_timeline.get());
    m_project  = std::make_unique<project::Project>();
    m_compare  = std::make_unique<playback::CompareSession>();
    m_apiServer = std::make_unique<api::ApiServer>(m_playback.get(), m_timeline.get());

    // The registry is parented to the window, so its QActions live exactly as
    // long as the widgets that reference them.
    m_commands = new CommandRegistry(this);

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
    column->addWidget(m_transport);

    setCentralWidget(central);

    // --- Left dock: sources ----------------------------------------------
    m_sources = new SourcesPanel(this);
    m_sources->setProject(m_project.get());

    m_sourcesDock = new QDockWidget(tr("Sources"), this);
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

    // --- Status bar -------------------------------------------------------
    m_statusInfo = new StatusInfoBar(this);
    m_statusInfo->setModel(m_timeline.get());
    statusBar()->addPermanentWidget(m_statusInfo, 1);
    statusBar()->setSizeGripEnabled(true);
}

void MainWindow::buildMenus()
{
    // Adding every action to the window itself gives the shortcuts
    // window-wide scope, so they fire regardless of which widget has focus.
    addActions(m_commands->allActions());

    const commands::CommandCategory categories[] = {
        commands::CommandCategory::File,
        commands::CommandCategory::Playback,
        commands::CommandCategory::Audio,
        commands::CommandCategory::View,
        commands::CommandCategory::Help,
    };

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
    if (QAction* snapAction = m_commands->action(CommandId::ToggleBookmarkSnap)) {
        snapAction->setChecked(true);
    }
}

void MainWindow::connectSignals()
{
    connect(m_commands, &CommandRegistry::commandTriggered,
            this, &MainWindow::onCommand);

    connect(m_playback.get(), &playback::PlaybackController::stateChanged,
            this, &MainWindow::onPlayerStateChanged);

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

    connect(m_timelineWidget, &TimelineWidget::bookmarkActivated,
            this, [this](qint64 frame) {
                m_timeline->ensureFrameVisible(frame);
                m_playback->seekFrame(frame);
            });

    connect(m_project.get(), &project::Project::modifiedChanged,
            this, [this](bool) { updateWindowTitle(); });

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
        m_timeline->addBookmark(bookmark);
        statusBar()->showMessage(tr("Bookmark added at frame %1").arg(bookmark.frame), 2000);
        return;
    }
    case CommandId::NextBookmark: {
        const int64_t frame = m_timeline->nextBookmarkFrame(m_timeline->currentFrame());
        if (frame >= 0) {
            m_timeline->ensureFrameVisible(frame);
            m_playback->seekFrame(frame);
        }
        return;
    }
    case CommandId::PreviousBookmark: {
        const int64_t frame = m_timeline->previousBookmarkFrame(m_timeline->currentFrame());
        if (frame >= 0) {
            m_timeline->ensureFrameVisible(frame);
            m_playback->seekFrame(frame);
        }
        return;
    }
    case CommandId::DeleteBookmark:
        m_timeline->removeBookmarkAt(m_timeline->currentFrame());
        return;
    case CommandId::ToggleBookmarkSnap:
        m_timelineWidget->setBookmarkSnapEnabled(checked);
        return;

    // --- View: fully wired ------------------------------------------------
    case CommandId::ZoomFit:
        m_viewer->setFitMode(ViewerWidget::FitMode::FitInWindow);
        return;
    case CommandId::ZoomActualSize:
        m_viewer->setFitMode(ViewerWidget::FitMode::ActualSize);
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

    // --- Application ------------------------------------------------------
    case CommandId::Quit:
        close();
        return;
    case CommandId::About:
        QMessageBox::about(
            this,
            tr("About ATK Player"),
            tr("<h3>%1 %2</h3>"
               "<p>An animation playback and review application.</p>"
               "<p>This is a development build. Media playback, projects and the "
               "external API are not implemented yet -- see the roadmap in the "
               "repository for the planned milestones.</p>")
                .arg(QString::fromLatin1(version::kApplicationName),
                     QString::fromLatin1(version::kString)));
        return;

    // --- Declared, not yet implemented ------------------------------------
    case CommandId::OpenMedia:
        openMediaDialog();
        return;
    case CommandId::CloseSource:
        m_playback->closeMedia();
        return;
    case CommandId::ToggleAudioScrub:
        m_playback->setAudioScrubEnabled(checked);
        statusBar()->showMessage(
            checked ? tr("Audio scrubbing on") : tr("Audio scrubbing off"), 1500);
        return;
    case CommandId::ToggleFrameStepAudio:
        m_playback->setFrameStepAudioEnabled(checked);
        statusBar()->showMessage(checked ? tr("Frame-step audio on") : tr("Frame-step audio off"), 1500);
        return;

    case CommandId::ToggleMute: {
        m_playback->setMuted(checked);
        statusBar()->showMessage(checked ? tr("Audio muted") : tr("Audio unmuted"), 1500);
        return;
    }
    case CommandId::VolumeUp:
        m_playback->setVolume(m_playback->volume() + 0.1);
        statusBar()->showMessage(
            tr("Volume %1%").arg(qRound(m_playback->volume() * 100)), 1500);
        return;
    case CommandId::VolumeDown:
        m_playback->setVolume(m_playback->volume() - 0.1);
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

    case CommandId::OpenProject:
    case CommandId::SaveProject:
    case CommandId::SaveProjectAs:
    case CommandId::ZoomIn:
    case CommandId::ZoomOut:
        reportNotImplemented(id);
        return;
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

void MainWindow::onPlayerStateChanged(playback::PlayerState state)
{
    using playback::PlayerState;

    m_transport->setPlaying(state == PlayerState::Playing);
    updateTransportEnabled();

    switch (state) {
    case PlayerState::Playing:
        statusBar()->showMessage(tr("Playing"), 1500);
        break;
    case PlayerState::Paused:
        statusBar()->showMessage(tr("Paused"), 1500);
        break;
    case PlayerState::Ended:
        statusBar()->showMessage(tr("End of media"), 2000);
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

    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(0, 100);
    slider->setValue(qRound(m_playback->volume() * 100));
    slider->setFixedWidth(140);
    layout->addWidget(slider);

    connect(slider, &QSlider::valueChanged, this, [this](int value) {
        m_playback->setVolume(value / 100.0);
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
    m_playback->openMedia(filePath);
}

void MainWindow::onMediaOpened(const media::MediaMetadata& metadata)
{
    m_viewer->setSourceAspectRatio(
        metadata.resolution.height() > 0
            ? (static_cast<double>(metadata.resolution.width()) * metadata.pixelAspectRatio)
                  / static_cast<double>(metadata.resolution.height())
            : 0.0);

    // The waveform is indexed by media time and the track by frame, so the
    // widget needs the duration to map between them.
    m_timelineWidget->setWaveform(&m_playback->waveform());
    m_timelineWidget->setMediaDuration(metadata.durationUs);

    m_sources->setCurrentMedia(metadata.fileName, metadata.shortDescription());
    m_statusInfo->setMediaInfo(metadata.fileName, metadata.hasExactFrameCount());

    updateTransportEnabled();
    updateWindowTitle();

    statusBar()->showMessage(tr("Opened %1").arg(metadata.fileName), 3000);
}

void MainWindow::onMediaError(const QString& message)
{
    m_sources->clearCurrentMedia();
    m_statusInfo->clearMediaInfo();
    updateTransportEnabled();
    updateWindowTitle();
    statusBar()->showMessage(message, 8000);
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("%1 — %2")
                        .arg(QString::fromLatin1(version::kApplicationName),
                             QString::fromLatin1(version::kString));

    if (m_project && m_project->isModified()) {
        title.append(QStringLiteral(" *"));
    }

    setWindowTitle(title);
}

} // namespace atk::ui
