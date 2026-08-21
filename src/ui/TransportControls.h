#pragma once

#include "core/commands/CommandId.h"

#include <QString>
#include <QIcon>
#include <QWidget>

class QToolButton;

namespace atk::ui {

class CommandRegistry;

/// The transport control strip: jump to start, step back, play/pause, step
/// forward, jump to end, loop.
///
/// Every button is backed by the QAction for its command, so the buttons, the
/// menu entries and the keyboard shortcuts are literally the same objects. The
/// bar therefore has no click handlers of its own -- it only reflects state.
class TransportControls : public QWidget {
    Q_OBJECT

public:
    /// `registry` supplies the actions and must outlive the bar.
    explicit TransportControls(CommandRegistry* registry, QWidget* parent = nullptr);
    ~TransportControls() override;

    /// Updates the play/pause button to match the transport.
    void setPlaying(bool playing);

private:
    QToolButton* makeCommandButton(commands::CommandId id,
                                   const QIcon& icon,
                                   const QString& tooltip);

    CommandRegistry* m_registry = nullptr;
    QToolButton* m_playPauseButton = nullptr;
    QIcon m_playIcon;
    QIcon m_pauseIcon;
};

} // namespace atk::ui
