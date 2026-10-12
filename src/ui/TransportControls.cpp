#include "ui/TransportControls.h"

#include "core/commands/CommandDefinitions.h"
#include "ui/IconProvider.h"
#include "ui/Theme.h"
#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QHBoxLayout>
#include <QToolButton>
#include <QMenu>
#include <QSlider>
#include <QLabel>
#include <QWidgetAction>

#include <algorithm>

namespace atk::ui {

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
    layout->addWidget(draw);

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

    // Loop is a mode rather than a transport move, so it sits apart.
    layout->addSpacing(14);
    layout->addWidget(makeCommandButton(commands::CommandId::ToggleLoop,
        icons::icon(QStringLiteral("repeat")), tr("Loop playback")));

    layout->addSpacing(10);
    m_volumeButton = new QToolButton(this);
    m_volumeButton->setObjectName(QStringLiteral("TransportVolumeButton"));
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
    m_volumeLabel = new QLabel(QStringLiteral("100%"), container); m_volumeLabel->setMinimumWidth(42);
    popupLayout->addWidget(m_volumeSlider); popupLayout->addWidget(m_volumeLabel);
    auto* sliderAction = new QWidgetAction(menu); sliderAction->setDefaultWidget(container); menu->addAction(sliderAction);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_volumeLabel->setText(QStringLiteral("%1%").arg(value)); emit volumeChanged(value);
    });
    m_volumeButton->setMenu(menu); layout->addWidget(m_volumeButton);

    layout->addStretch(1);

    setPlaying(false);
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
