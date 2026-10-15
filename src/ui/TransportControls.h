#pragma once

#include "core/commands/CommandId.h"
#include "media/MediaMetadata.h"

#include <QString>
#include <QIcon>
#include <QWidget>

class QHBoxLayout;
class QToolButton;
class QSlider;
class QLabel;

namespace atk::ui {

class CommandRegistry;

/// The strip under the timeline, in groups: Draw on Frame | in, out, loop and
/// playback speed | the transport (first, step, skip, play/pause, skip, step,
/// last) | view tools (tracing paper, ghosts, hold drawings, drag markers,
/// flip, show drawings) | zoom, fit, video full screen, preferences and volume.
///
/// Every button is backed by the QAction for its command, so the buttons, the
/// menu entries and the keyboard shortcuts are literally the same objects. The
/// only click handlers of its own are the speed and zoom presets in the speed
/// and zoom menus, which it reports as requests.
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
    /// Shows the playback speed on the speed button as the frame rate it
    /// plays at ("24fps", "12fps"), like SyncSketch, and checks it in the menu.
    void setPlaybackSpeed(double speed, const media::FrameRate& rate);
    /// The viewer zoom shown on the zoom button ("Fit", "150%").
    void setZoomText(const QString& text);
    /// "24fps" for 24/1 at 1.0, "11.988fps" for 24000/1001 at 0.5.
    static QString speedLabel(double speed, const media::FrameRate& rate);
    /// Adds widgets owned elsewhere (the tracing paper and ghost buttons with
    /// their menus) to the view tools group, before flip.
    void addViewTools(const QList<QWidget*>& widgets);

signals:
    void volumeChanged(int percent);
    /// A speed (multiple of the media rate) was picked from the speed menu.
    void playbackSpeedRequested(double speed);
    /// A zoom level (100 = actual size) was picked from the zoom menu.
    void zoomPercentRequested(qreal percent);

private:
    QToolButton* makeCommandButton(commands::CommandId id,
                                   const QIcon& icon,
                                   const QString& tooltip);

    CommandRegistry* m_registry = nullptr;
    QToolButton* m_playPauseButton = nullptr;
    QToolButton* m_volumeButton = nullptr;
    QToolButton* m_speedButton = nullptr;
    QToolButton* m_zoomButton = nullptr;
    QList<QAction*> m_speedActions;
    QHBoxLayout* m_viewTools = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeLabel = nullptr;
    QIcon m_playIcon;
    QIcon m_pauseIcon;
};

} // namespace atk::ui
