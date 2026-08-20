#include "ui/TransportControls.h"

#include "core/commands/CommandDefinitions.h"
#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QHBoxLayout>
#include <QToolButton>

namespace atk::ui {
namespace {

// Glyphs stand in for icons until artwork lands in assets/icons (milestone M2).
// Chosen from ranges present in the default Windows, macOS and Linux UI fonts.
const QString kGlyphFirst    = QStringLiteral("|◀◀");
const QString kGlyphPrevious = QStringLiteral("◀|");
const QString kGlyphPlay     = QStringLiteral("▶");
const QString kGlyphPause    = QStringLiteral("❘❘");
const QString kGlyphNext     = QStringLiteral("|▶");
const QString kGlyphLast     = QStringLiteral("▶▶|");
const QString kGlyphLoop     = QStringLiteral("↻");

} // namespace

TransportControls::TransportControls(CommandRegistry* registry, QWidget* parent)
    : QWidget(parent)
    , m_registry(registry)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    layout->addStretch(1);
    layout->addWidget(makeCommandButton(commands::CommandId::FirstFrame,    kGlyphFirst,    tr("Jump to first frame")));
    layout->addWidget(makeCommandButton(commands::CommandId::PreviousFrame, kGlyphPrevious, tr("Previous frame")));

    m_playPauseButton = makeCommandButton(commands::CommandId::PlayPause, kGlyphPlay, tr("Play"));
    layout->addWidget(m_playPauseButton);

    layout->addWidget(makeCommandButton(commands::CommandId::NextFrame, kGlyphNext, tr("Next frame")));
    layout->addWidget(makeCommandButton(commands::CommandId::LastFrame, kGlyphLast, tr("Jump to last frame")));

    // Loop is a mode rather than a transport move, so it sits apart.
    layout->addSpacing(14);
    layout->addWidget(makeCommandButton(commands::CommandId::ToggleLoop, kGlyphLoop, tr("Loop playback")));

    layout->addStretch(1);

    setPlaybackState(playback::PlaybackState::Stopped);
}

TransportControls::~TransportControls() = default;

QToolButton* TransportControls::makeCommandButton(commands::CommandId id,
                                             const QString& glyph,
                                             const QString& tooltip)
{
    auto* button = new QToolButton(this);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // Transport buttons must not steal focus, or Space would re-trigger the
    // focused button instead of running the Play/Pause command.
    button->setFocusPolicy(Qt::NoFocus);

    QAction* action = m_registry != nullptr ? m_registry->action(id) : nullptr;
    if (action != nullptr) {
        button->setDefaultAction(action);

        // setDefaultAction() keeps the button text in sync with the action, and
        // the action carries menu wording. Re-apply the glyph whenever the
        // action changes so a shortcut edit cannot replace the icon with words.
        const auto applyGlyph = [button, glyph, tooltip, action] {
            const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
            button->setText(glyph);
            button->setToolTip(shortcut.isEmpty()
                                   ? tooltip
                                   : QStringLiteral("%1 (%2)").arg(tooltip, shortcut));
        };
        connect(action, &QAction::changed, button, applyGlyph);
        applyGlyph();
    } else {
        button->setText(glyph);
        button->setToolTip(tooltip);
    }

    return button;
}

void TransportControls::setPlaybackState(playback::PlaybackState state)
{
    if (m_playPauseButton == nullptr) {
        return;
    }

    const bool playing = state == playback::PlaybackState::Playing;
    m_playPauseButton->setText(playing ? kGlyphPause : kGlyphPlay);

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
