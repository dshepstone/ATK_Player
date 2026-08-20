#pragma once

#include "media/MediaMetadata.h"

#include <QElapsedTimer>

#include <cstdint>

namespace atk::playback {

/// The master timing source for playback.
///
/// The clock converts elapsed monotonic time into a frame number. It is
/// deliberately the only thing that decides "what frame should be on screen
/// now": frames are never advanced by counting timer ticks, because a dropped
/// or late tick would then desynchronise playback from audio permanently.
///
/// A/B comparison uses ONE clock. Both viewers ask this same instance for the
/// current frame and apply their own per-source offset, so the two sources can
/// never drift apart no matter how differently they decode.
class PlaybackClock {
public:
    PlaybackClock() = default;

    void setFrameRate(media::FrameRate rate);
    media::FrameRate frameRate() const { return m_frameRate; }

    /// Playback speed multiplier; 1.0 is real time. Must be > 0.
    void setSpeed(double speed);
    double speed() const { return m_speed; }

    /// Starts running from `startFrame`.
    void start(int64_t startFrame);
    /// Freezes the frame number; resume() continues from there.
    void pause();
    void resume();
    /// Stops and forgets elapsed time. currentFrame() returns the anchor frame.
    void stop();

    bool isRunning() const { return m_running; }

    /// Re-anchors the clock to `frame` without changing running state. Used by
    /// seeks so playback continues smoothly from the new position.
    void seekTo(int64_t frame);

    /// The frame that should be displayed now.
    int64_t currentFrame() const;

    /// Microseconds of playback time since the current anchor.
    int64_t elapsedUs() const;

private:
    media::FrameRate m_frameRate;
    double m_speed = 1.0;
    /// Frame the clock was last anchored at (start, seek or pause).
    int64_t m_anchorFrame = 0;
    /// Elapsed playback time accumulated before the current run segment.
    int64_t m_accumulatedUs = 0;
    QElapsedTimer m_timer;
    bool m_running = false;
};

} // namespace atk::playback
