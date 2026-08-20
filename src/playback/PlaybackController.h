#pragma once

#include "media/MediaSource.h"
#include "playback/PlaybackClock.h"
#include "timeline/PlaybackRange.h"

#include <QObject>

#include <cstdint>
#include <memory>

class QTimer;

namespace atk::timeline { class TimelineModel; }

namespace atk::playback {

/// Transport state of the player.
enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
};

/// Drives playback: owns the master clock, advances the timeline playhead and
/// decides which frame should be on screen.
///
/// This class has no UI dependency. Widgets observe its signals and call its
/// methods; it never calls into a widget. That separation is what will let the
/// external API (src/api) and the DCC integrations drive the exact same
/// transport as the on-screen buttons, with no duplicated logic.
///
/// PHASE 0 STATUS: state transitions, stepping, seeking, looping and range
/// handling are real and observable. No frames are decoded, because
/// FFmpegDecoder is still a stub -- so playing with media loaded advances the
/// playhead but the viewer shows its empty state.
class PlaybackController : public QObject {
    Q_OBJECT

public:
    /// `timeline` must outlive the controller and is not owned by it.
    explicit PlaybackController(timeline::TimelineModel* timeline, QObject* parent = nullptr);
    ~PlaybackController() override;

    // --- Transport --------------------------------------------------------
    void play();
    void pause();
    void togglePlayPause();
    void stop();

    void seekFrame(int64_t frame);
    void stepForward();
    void stepBackward();
    void goToStart();
    void goToEnd();

    // --- Options ----------------------------------------------------------
    void setLoopEnabled(bool enabled);
    bool isLoopEnabled() const { return m_loopEnabled; }

    /// Convenience wrapper writing straight through to the timeline model.
    void setPlaybackRange(int64_t startFrame, int64_t endFrame);
    void clearPlaybackRange();

    /// Playback speed multiplier, 1.0 = real time.
    void setSpeed(double speed);
    double speed() const { return m_clock.speed(); }

    // --- Media ------------------------------------------------------------
    /// Takes ownership. Passing nullptr unloads the current source.
    void setSource(std::shared_ptr<media::MediaSource> source);
    const std::shared_ptr<media::MediaSource>& source() const { return m_source; }
    bool hasMedia() const;

    // --- State ------------------------------------------------------------
    PlaybackState state() const { return m_state; }
    bool isPlaying() const { return m_state == PlaybackState::Playing; }
    int64_t currentFrame() const;

signals:
    void stateChanged(atk::playback::PlaybackState state);
    void loopEnabledChanged(bool enabled);
    void sourceChanged();
    /// Emitted when playback reaches the end with looping disabled.
    void playbackFinished();

private:
    void onTick();
    void setState(PlaybackState state);
    /// Interval derived from the frame rate, clamped to something sane when no
    /// media is loaded so the transport still behaves.
    int tickIntervalMs() const;

    timeline::TimelineModel* m_timeline = nullptr;
    std::shared_ptr<media::MediaSource> m_source;
    PlaybackClock m_clock;
    QTimer* m_tickTimer = nullptr;
    PlaybackState m_state = PlaybackState::Stopped;
    bool m_loopEnabled = false;
};

} // namespace atk::playback

Q_DECLARE_METATYPE(atk::playback::PlaybackState)
