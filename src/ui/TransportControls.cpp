#include "ui/TransportControls.h"

#include "core/commands/CommandDefinitions.h"
#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QHBoxLayout>
#include <QToolButton>
#include <QStyle>

namespace atk::ui {
TransportControls::TransportControls(CommandRegistry* registry, QWidget* parent)
    : QWidget(parent)
    , m_registry(registry)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_playIcon = style()->standardIcon(QStyle::SP_MediaPlay);
    m_pauseIcon = style()->standardIcon(QStyle::SP_MediaPause);

    layout->addStretch(1);
    layout->addWidget(makeCommandButton(commands::CommandId::FirstFrame,
        style()->standardIcon(QStyle::SP_MediaSkipBackward), tr("Jump to first frame")));
    layout->addWidget(makeCommandButton(commands::CommandId::PreviousFrame,
        style()->standardIcon(QStyle::SP_MediaSeekBackward), tr("Previous frame")));

    m_playPauseButton = makeCommandButton(commands::CommandId::PlayPause, m_playIcon, tr("Play"));
    layout->addWidget(m_playPauseButton);

    layout->addWidget(makeCommandButton(commands::CommandId::NextFrame,
        style()->standardIcon(QStyle::SP_MediaSeekForward), tr("Next frame")));
    layout->addWidget(makeCommandButton(commands::CommandId::LastFrame,
        style()->standardIcon(QStyle::SP_MediaSkipForward), tr("Jump to last frame")));

    // Loop is a mode rather than a transport move, so it sits apart.
    layout->addSpacing(14);
    layout->addWidget(makeCommandButton(commands::CommandId::ToggleLoop,
        style()->standardIcon(QStyle::SP_BrowserReload), tr("Loop playback")));

    layout->addStretch(1);

    setPlaying(false);
}

TransportControls::~TransportControls() = default;

QToolButton* TransportControls::makeCommandButton(commands::CommandId id,
                                             const QIcon& icon,
                                             const QString& tooltip)
{
    auto* button = new QToolButton(this);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(18, 18));
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
