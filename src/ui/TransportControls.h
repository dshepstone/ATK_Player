#pragma once

#include "core/commands/CommandId.h"

#include <QString>
#include <QIcon>
#include <QWidget>

class QHBoxLayout;
class QToolButton;
class QSlider;
class QLabel;

namespace atk::ui {

class CommandRegistry;

/// The strip under the timeline, in groups: Draw on Frame | in, out and loop |
/// the transport (first, step, skip, play/pause, skip, step, last) | view
/// tools (tracing paper, ghosts, flip, show drawings) | fit, video full screen,
/// preferences and volume.
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
    void setVolumePercent(int percent);
    void setMuted(bool muted);
    /// Adds widgets owned elsewhere (the tracing paper and ghost buttons with
    /// their menus) to the view tools group, before flip.
    void addViewTools(const QList<QWidget*>& widgets);

signals:
    void volumeChanged(int percent);

private:
    QToolButton* makeCommandButton(commands::CommandId id,
                                   const QIcon& icon,
                                   const QString& tooltip);

    CommandRegistry* m_registry = nullptr;
    QToolButton* m_playPauseButton = nullptr;
    QToolButton* m_volumeButton = nullptr;
    QHBoxLayout* m_viewTools = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeLabel = nullptr;
    QIcon m_playIcon;
    QIcon m_pauseIcon;
};

} // namespace atk::ui
