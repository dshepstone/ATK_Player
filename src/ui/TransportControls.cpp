#include "ui/TransportControls.h"

#include "core/commands/CommandDefinitions.h"
#include "playback/PlaybackController.h"
#include "ui/IconProvider.h"
#include "ui/Theme.h"
#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QFontMetrics>
#include <QActionGroup>
#include <QLocale>
#include <QHBoxLayout>
#include <QToolButton>
#include <QMenu>
#include <QSlider>
#include <QStyle>
#include <QLabel>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>

namespace atk::ui {
namespace {

/// Gives a text-beside-icon button the width of its widest label, so the
/// label changing (a new frame rate, a zoom level) never changes the bar's
/// size and with it the window's minimum width.
void fixWidthForText(QToolButton* button, const QString& widest)
{
    const QFontMetrics metrics(button->font());
    const int width = button->iconSize().width() + metrics.horizontalAdvance(widest) + 28;
    button->setFixedWidth(width);
}

} // namespace

TransportControls::TransportControls(CommandRegistry* registry, QWidget* parent)
    : QWidget(parent)
    , m_registry(registry)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_playIcon = icons::icon(QStringLiteral("play"));
    m_pauseIcon = icons::icon(QStringLiteral("pause"));

    // Drawing tools: on the left, apart from the transport, like SyncSketch.
    QToolButton* draw = makeCommandButton(commands::CommandId::ToggleAnnotateMode,
        icons::icon(QStringLiteral("pencil")), tr("Draw on frame: show the drawing tools"));
    draw->setObjectName(QStringLiteral("TransportDrawButton"));
    draw->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    draw->setProperty("atkRole", QVariant()); // a framed button: the way into drawing
    layout->addWidget(draw);

    // In, out and loop: the review range.
    auto separator = [this, layout] {
        auto* line = new QWidget(this);
        line->setFixedSize(1, 20);
        line->setStyleSheet(QStringLiteral("background-color: %1;").arg(theme::controlHover().name()));
        layout->addSpacing(6);
        layout->addWidget(line);
        layout->addSpacing(6);
    };
    separator();
    layout->addWidget(makeCommandButton(commands::CommandId::SetRangeIn,
        icons::icon(QStringLiteral("arrow-left-to-line")), tr("Set range in")));
    layout->addWidget(makeCommandButton(commands::CommandId::SetRangeOut,
        icons::icon(QStringLiteral("arrow-right-to-line")), tr("Set range out")));
    // Loop, with the arrow choosing what happens at the end of each clip.
    QToolButton* loop = makeCommandButton(commands::CommandId::ToggleLoop, icons::icon(QStringLiteral("repeat")),
        tr("Loop. The arrow chooses: play through the playlist, stop after each clip, "
           "loop this clip or loop the whole playlist."));
    loop->setObjectName(QStringLiteral("TransportLoopButton"));
    loop->setPopupMode(QToolButton::MenuButtonPopup);
    auto* loopMenu = new QMenu(loop);
    loopMenu->setObjectName(QStringLiteral("TransportLoopMenu"));
    loopMenu->addSection(tr("Loop off"));
    for (const auto id : {commands::CommandId::PlaylistPlayThrough, commands::CommandId::PlaylistStopAfterClip})
        if (QAction* action = m_registry ? m_registry->action(id) : nullptr) loopMenu->addAction(action);
    loopMenu->addSection(tr("Loop on"));
    for (const auto id : {commands::CommandId::PlaylistLoopClip, commands::CommandId::PlaylistLoopAll})
        if (QAction* action = m_registry ? m_registry->action(id) : nullptr) loopMenu->addAction(action);
    loop->setMenu(loopMenu);
    layout->addWidget(loop);

    // Playback speed: shows the rate it plays at; the menu picks a speed.
    m_speedButton = new QToolButton(this);
    m_speedButton->setObjectName(QStringLiteral("TransportSpeedButton"));
    m_speedButton->setAccessibleName(tr("Playback speed"));
    m_speedButton->setIcon(icons::icon(QStringLiteral("gauge")));
    m_speedButton->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    m_speedButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_speedButton->setPopupMode(QToolButton::InstantPopup);
    m_speedButton->setFocusPolicy(Qt::NoFocus);
    m_speedButton->setProperty("atkRole", "iconButton");
    auto* speedMenu = new QMenu(m_speedButton);
    speedMenu->setObjectName(QStringLiteral("TransportSpeedMenu"));
    speedMenu->addSection(tr("Playback speed"));
    for (const auto id : {commands::CommandId::DecreasePlaybackSpeed, commands::CommandId::IncreasePlaybackSpeed,
                          commands::CommandId::ResetPlaybackSpeed})
        if (QAction* action = m_registry ? m_registry->action(id) : nullptr) speedMenu->addAction(action);
    speedMenu->addSeparator();
    auto* speedGroup = new QActionGroup(speedMenu);
    speedGroup->setExclusive(true);
    for (const double speed : playback::PlaybackController::playbackSpeeds()) {
        QAction* action = speedMenu->addAction(QString());
        action->setCheckable(true);
        action->setData(speed);
        speedGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, speed] { emit playbackSpeedRequested(speed); });
        m_speedActions.append(action);
    }
    m_speedButton->setMenu(speedMenu);
    fixWidthForText(m_speedButton, QStringLiteral("99.999fps"));
    layout->addWidget(m_speedButton);

    layout->addStretch(1);
    layout->addWidget(makeCommandButton(commands::CommandId::FirstFrame,
        icons::icon(QStringLiteral("skip-back")), tr("Jump to first frame")));
    layout->addWidget(makeCommandButton(commands::CommandId::PreviousFrame,
        icons::icon(QStringLiteral("step-back")), tr("Previous frame")));
    layout->addWidget(makeCommandButton(commands::CommandId::SkipBack10Seconds,
        icons::icon(QStringLiteral("rewind")), tr("Skip back 10 seconds")));

    m_playPauseButton = makeCommandButton(commands::CommandId::PlayPause, m_playIcon, tr("Play"));
    layout->addWidget(m_playPauseButton);

    layout->addWidget(makeCommandButton(commands::CommandId::NextFrame,
        icons::icon(QStringLiteral("step-forward")), tr("Next frame")));
    layout->addWidget(makeCommandButton(commands::CommandId::SkipForward10Seconds,
        icons::icon(QStringLiteral("fast-forward")), tr("Skip forward 10 seconds")));
    layout->addWidget(makeCommandButton(commands::CommandId::LastFrame,
        icons::icon(QStringLiteral("skip-forward")), tr("Jump to last frame")));

    layout->addStretch(1);
    // View tools: how the picture is shown.
    m_viewTools = new QHBoxLayout;
    m_viewTools->setSpacing(2);
    layout->addLayout(m_viewTools);
    // Hold drawings, with the arrow choosing what drawing on a held frame does.
    QToolButton* hold = makeCommandButton(commands::CommandId::ToggleHoldDrawings, icons::icon(QStringLiteral("pin")),
        tr("Hold drawings until the next drawing. The arrow chooses whether drawing on a held "
           "frame adds to the held drawing or starts a new one."));
    hold->setObjectName(QStringLiteral("TransportHoldButton"));
    hold->setPopupMode(QToolButton::MenuButtonPopup);
    auto* holdMenu = new QMenu(hold);
    holdMenu->setObjectName(QStringLiteral("TransportHoldMenu"));
    holdMenu->addSection(tr("Hold drawings"));
    for (const auto id : {commands::CommandId::HoldDrawingsAddToCurrent, commands::CommandId::HoldDrawingsAddNew,
                          commands::CommandId::HoldDrawingsOff})
        if (QAction* action = m_registry ? m_registry->action(id) : nullptr) holdMenu->addAction(action);
    hold->setMenu(holdMenu);
    m_viewTools->addWidget(hold);
    QToolButton* drag = makeCommandButton(commands::CommandId::ToggleDragMarkers,
        icons::icon(QStringLiteral("unfold-horizontal")),
        tr("Drag markers: move a frame's notes and drawings by dragging its marker under the timeline"));
    drag->setObjectName(QStringLiteral("TransportDragMarkersButton"));
    m_viewTools->addWidget(drag);
    m_viewTools->addWidget(makeCommandButton(commands::CommandId::FlipHorizontal,
        icons::icon(QStringLiteral("flip-horizontal-2")), tr("Flip canvas")));
    m_viewTools->addWidget(makeCommandButton(commands::CommandId::ToggleAnnotationsVisible,
        icons::icon(QStringLiteral("eye")), tr("Show drawings")));
    separator();
    // Zoom: shows the level; the menu zooms in and out or picks a level.
    m_zoomButton = new QToolButton(this);
    m_zoomButton->setObjectName(QStringLiteral("TransportZoomButton"));
    m_zoomButton->setAccessibleName(tr("Zoom"));
    m_zoomButton->setIcon(icons::icon(QStringLiteral("zoom-in")));
    m_zoomButton->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    m_zoomButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_zoomButton->setPopupMode(QToolButton::InstantPopup);
    m_zoomButton->setFocusPolicy(Qt::NoFocus);
    m_zoomButton->setProperty("atkRole", "iconButton");
    m_zoomButton->setToolTip(tr("Change the zoom level"));
    auto* zoomMenu = new QMenu(m_zoomButton);
    zoomMenu->setObjectName(QStringLiteral("TransportZoomMenu"));
    zoomMenu->addSection(tr("Zoom"));
    for (const auto id : {commands::CommandId::ZoomIn, commands::CommandId::ZoomOut, commands::CommandId::ZoomFit,
                          commands::CommandId::ZoomActualSize})
        if (QAction* action = m_registry ? m_registry->action(id) : nullptr) zoomMenu->addAction(action);
    zoomMenu->addSeparator();
    for (const int percent : {25, 50, 200, 400}) {
        QAction* action = zoomMenu->addAction(tr("%1%").arg(percent));
        connect(action, &QAction::triggered, this, [this, percent] { emit zoomPercentRequested(percent); });
    }
    m_zoomButton->setMenu(zoomMenu);
    setZoomText(tr("Fit"));
    fixWidthForText(m_zoomButton, QStringLiteral("800%"));
    layout->addWidget(m_zoomButton);
    layout->addWidget(makeCommandButton(commands::CommandId::ZoomFit,
        icons::icon(QStringLiteral("scan")), tr("Fit the picture to the viewer")));
    layout->addWidget(makeCommandButton(commands::CommandId::ToggleVideoFullScreen,
        icons::icon(QStringLiteral("fullscreen")), tr("Video full screen")));
    layout->addWidget(makeCommandButton(commands::CommandId::Preferences,
        icons::icon(QStringLiteral("settings")), tr("Preferences")));

    m_volumeButton = new QToolButton(this);
    m_volumeButton->setObjectName(QStringLiteral("TransportVolumeButton"));
    m_volumeButton->setAccessibleName(tr("Volume"));
    m_volumeButton->setIcon(icons::icon(QStringLiteral("volume-2")));
    m_volumeButton->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    m_volumeButton->setToolTip(tr("Volume"));
    m_volumeButton->setPopupMode(QToolButton::InstantPopup);
    m_volumeButton->setFocusPolicy(Qt::NoFocus);
    auto* menu = new QMenu(m_volumeButton);
    if (QAction* mute = m_registry->action(commands::CommandId::ToggleMute)) menu->addAction(mute);
    auto* container = new QWidget(menu); auto* popupLayout = new QHBoxLayout(container);
    popupLayout->setContentsMargins(10, 6, 10, 6);
    m_volumeSlider = new QSlider(Qt::Horizontal, container); m_volumeSlider->setRange(0, 100); m_volumeSlider->setFixedWidth(140);
    m_volumeSlider->setAccessibleName(tr("Volume level"));
    m_volumeLabel = new QLabel(QStringLiteral("100%"), container); m_volumeLabel->setMinimumWidth(42);
    popupLayout->addWidget(m_volumeSlider); popupLayout->addWidget(m_volumeLabel);
    auto* sliderAction = new QWidgetAction(menu); sliderAction->setDefaultWidget(container); menu->addAction(sliderAction);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_volumeLabel->setText(QStringLiteral("%1%").arg(value)); emit volumeChanged(value);
    });
    m_volumeButton->setMenu(menu); layout->addWidget(m_volumeButton);
    m_volumeButton->setProperty("atkRole", "iconButton");

    setPlaying(false);
    setPlaybackSpeed(1.0, {});
}

QString TransportControls::speedLabel(double speed, const media::FrameRate& rate)
{
    if (!rate.isValid()) {
        // No media: say the multiple instead of a rate.
        return speed == 1.0 ? tr("1x") : tr("%1x").arg(QLocale::c().toString(speed, 'g', 3));
    }
    // Up to three decimals, without trailing zeros: 24, 23.976, 11.988.
    QString fps = QString::number(rate.toDouble() * speed, 'f', 3);
    while (fps.endsWith(QLatin1Char('0'))) fps.chop(1);
    if (fps.endsWith(QLatin1Char('.'))) fps.chop(1);
    return tr("%1fps").arg(fps);
}

void TransportControls::setPlaybackSpeed(double speed, const media::FrameRate& rate)
{
    if (!m_speedButton) return;
    m_speedButton->setText(speedLabel(speed, rate));
    m_speedButton->setToolTip(speed == 1.0
        ? tr("Playback speed: normal")
        : tr("Playback speed: %1x of normal, without sound").arg(QLocale::c().toString(speed, 'g', 3)));
    for (QAction* action : m_speedActions) {
        const double value = action->data().toDouble();
        const QString multiple = QLocale::c().toString(value, 'g', 3);
        action->setText(rate.isValid() ? tr("%1x  (%2)").arg(multiple, speedLabel(value, rate))
                                       : tr("%1x").arg(multiple));
        action->setChecked(std::abs(value - speed) < 1e-9);
    }
}

void TransportControls::setZoomText(const QString& text)
{
    if (m_zoomButton) m_zoomButton->setText(text);
}

void TransportControls::addViewTools(const QList<QWidget*>& widgets)
{
    for (int i = 0; i < widgets.size(); ++i) {
        QWidget* widget = widgets[i];
        widget->setParent(this);
        widget->setProperty("atkRole", "iconButton");
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        m_viewTools->insertWidget(i, widget);
        widget->show();
    }
}

void TransportControls::setVolumePercent(int percent)
{
    if (m_volumeSlider) m_volumeSlider->setValue(std::clamp(percent, 0, 100));
}

void TransportControls::setMuted(bool muted)
{
    if (!m_volumeButton) return;
    m_volumeButton->setIcon(icons::icon(muted ? QStringLiteral("volume-x") : QStringLiteral("volume-2")));
    m_volumeButton->setToolTip(muted ? tr("Volume (Muted)") : tr("Volume"));
}

TransportControls::~TransportControls() = default;

QToolButton* TransportControls::makeCommandButton(commands::CommandId id,
                                             const QIcon& icon,
                                             const QString& tooltip)
{
    auto* button = new QToolButton(this);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Flat until hovered; a filled accent tile when on (loop, flip...).
    button->setProperty("atkRole", "iconButton");
    button->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    // Transport buttons must not steal focus, or Space would re-trigger the
    // focused button instead of running the Play/Pause command.
    button->setFocusPolicy(Qt::NoFocus);

    QAction* action = m_registry != nullptr ? m_registry->action(id) : nullptr;
    if (action != nullptr) {
        button->setDefaultAction(action);
        action->setIcon(icon);

        // setDefaultAction() keeps the button text in sync with the action, and
        // the action carries menu wording. Re-apply the glyph whenever the
        // action changes so a shortcut edit cannot replace the icon with words.
        const auto refreshTooltip = [button, tooltip, action] {
            const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
            button->setToolTip(shortcut.isEmpty()
                                   ? tooltip
                                   : QStringLiteral("%1 (%2)").arg(tooltip, shortcut));
        };
        connect(action, &QAction::changed, button, refreshTooltip);
        refreshTooltip();
    } else {
        button->setIcon(icon);
        button->setToolTip(tooltip);
    }

    return button;
}

void TransportControls::setPlaying(bool playing)
{
    if (m_playPauseButton == nullptr) {
        return;
    }

    m_playPauseButton->setIcon(playing ? m_pauseIcon : m_playIcon);

    const QString label = playing ? tr("Pause") : tr("Play");
    QAction* action = m_playPauseButton->defaultAction();
    const QString shortcut = action != nullptr
        ? action->shortcut().toString(QKeySequence::NativeText)
        : QString();

    m_playPauseButton->setToolTip(shortcut.isEmpty()
                                      ? label
                                      : QStringLiteral("%1 (%2)").arg(label, shortcut));
}

} // namespace atk::ui
