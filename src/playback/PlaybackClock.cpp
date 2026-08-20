#include "playback/PlaybackClock.h"

#include <algorithm>

namespace atk::playback {

void PlaybackClock::setFrameRate(media::FrameRate rate)
{
    // Re-anchor first so frames already elapsed are not reinterpreted at the
    // new rate.
    if (m_running || m_accumulatedUs != 0) {
        seekTo(currentFrame());
    }
    m_frameRate = rate;
}

void PlaybackClock::setSpeed(double speed)
{
    if (speed <= 0.0) {
        return;
    }
    if (m_running || m_accumulatedUs != 0) {
        seekTo(currentFrame());
    }
    m_speed = speed;
}

void PlaybackClock::start(int64_t startFrame)
{
    m_anchorFrame = startFrame;
    m_accumulatedUs = 0;
    m_timer.start();
    m_running = true;
}

void PlaybackClock::pause()
{
    if (!m_running) {
        return;
    }
    m_accumulatedUs += m_timer.nsecsElapsed() / 1000;
    m_running = false;
}

void PlaybackClock::resume()
{
    if (m_running) {
        return;
    }
    m_timer.start();
    m_running = true;
}

void PlaybackClock::stop()
{
    m_running = false;
    m_accumulatedUs = 0;
}

void PlaybackClock::seekTo(int64_t frame)
{
    m_anchorFrame = frame;
    m_accumulatedUs = 0;
    if (m_running) {
        m_timer.start();
    }
}

int64_t PlaybackClock::elapsedUs() const
{
    const int64_t live = m_running ? (m_timer.nsecsElapsed() / 1000) : 0;
    return m_accumulatedUs + live;
}

int64_t PlaybackClock::currentFrame() const
{
    if (!m_frameRate.isValid()) {
        return m_anchorFrame;
    }

    // frames = elapsed_us * speed * (num / den) / 1'000'000
    const double elapsedSeconds = static_cast<double>(elapsedUs()) / 1'000'000.0;
    const double frames = elapsedSeconds * m_speed * m_frameRate.toDouble();

    return m_anchorFrame + static_cast<int64_t>(frames);
}

} // namespace atk::playback
