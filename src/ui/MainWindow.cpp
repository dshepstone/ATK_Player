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
#include "ui/TransportControls.h"
#include "ui/ViewerWidget.h"
#include "ui/commands/CommandRegistry.h"

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
    m_timeline->setPlaceholderExtent(kPlaceholderFrameCount,
                                     media::FrameRate::fromInteger(kPlaceholderFps));

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
}

void MainWindow::connectSignals()
{
    connect(m_commands, &CommandRegistry::commandTriggered,
            this, &MainWindow::onCommand);

    connect(m_playback.get(), &playback::PlaybackController::stateChanged,
            this, &MainWindow::onPlaybackStateChanged);

    connect(m_playback.get(), &playback::PlaybackController::loopEnabledChanged,
            this, [this](bool enabled) {
                if (QAction* action = m_commands->action(CommandId::ToggleLoop)) {
                    QSignalBlocker blocker(action);
                    action->setChecked(enabled);
                }
            });

    // Scrubbing the timeline goes through the controller, not straight into the
    // model, so a drag behaves exactly like an API seek.
    connect(m_timelineWidget, &TimelineWidget::seekRequested,
            this, [this](qint64 frame) { m_playback->seekFrame(frame); });

    connect(m_timelineWidget, &TimelineWidget::bookmarkActivated,
            this, [this](qint64 frame) { m_playback->seekFrame(frame); });

    connect(m_project.get(), &project::Project::modifiedChanged,
            this, [this](bool) { updateWindowTitle(); });

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
            m_playback->seekFrame(frame);
        }
        return;
    }
    case CommandId::PreviousBookmark: {
        const int64_t frame = m_timeline->previousBookmarkFrame(m_timeline->currentFrame());
        if (frame >= 0) {
            m_playback->seekFrame(frame);
        }
        return;
    }

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
    case CommandId::OpenProject:
    case CommandId::SaveProject:
    case CommandId::SaveProjectAs:
    case CommandId::CloseSource:
    case CommandId::ToggleMute:
    case CommandId::VolumeUp:
    case CommandId::VolumeDown:
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

void MainWindow::onPlaybackStateChanged(playback::PlaybackState state)
{
    m_transport->setPlaybackState(state);

    switch (state) {
    case playback::PlaybackState::Playing:
        statusBar()->showMessage(tr("Playing"), 1500);
        break;
    case playback::PlaybackState::Paused:
        statusBar()->showMessage(tr("Paused"), 1500);
        break;
    case playback::PlaybackState::Stopped:
        statusBar()->showMessage(tr("Stopped"), 1500);
        break;
    }
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
