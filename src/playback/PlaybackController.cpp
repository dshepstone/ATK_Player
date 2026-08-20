#include "playback/PlaybackController.h"

#include "core/Logging.h"
#include "timeline/TimelineModel.h"

#include <QTimer>

#include <algorithm>
#include <utility>

namespace atk::playback {
namespace {

/// Tick rate used when no media is loaded. Fast enough that the transport feels
/// responsive, slow enough that an idle player costs nothing.
constexpr int kIdleTickIntervalMs = 33;

/// Upper bound on tick interval so a very low frame rate still repaints.
constexpr int kMaxTickIntervalMs = 200;

} // namespace

PlaybackController::PlaybackController(timeline::TimelineModel* timeline, QObject* parent)
    : QObject(parent)
    , m_timeline(timeline)
    , m_tickTimer(new QTimer(this))
{
    Q_ASSERT(m_timeline != nullptr);

    m_tickTimer->setTimerType(Qt::PreciseTimer);
    connect(m_tickTimer, &QTimer::timeout, this, &PlaybackController::onTick);

    connect(m_timeline, &timeline::TimelineModel::frameRateChanged,
            this, [this](media::FrameRate rate) {
                m_clock.setFrameRate(rate);
                if (m_tickTimer->isActive()) {
                    m_tickTimer->setInterval(tickIntervalMs());
                }
            });
}

PlaybackController::~PlaybackController() = default;

bool PlaybackController::hasMedia() const
{
    return m_source != nullptr;
}

int64_t PlaybackController::currentFrame() const
{
    return m_timeline->currentFrame();
}

void PlaybackController::play()
{
    if (m_state == PlaybackState::Playing) {
        return;
    }

    // When stopped at the very end, restart from the beginning rather than
    // playing zero frames.
    if (m_state == PlaybackState::Stopped
        && m_timeline->currentFrame() >= m_timeline->effectiveEndFrame()
        && m_timeline->frameCount() > 0) {
        m_timeline->setCurrentFrame(m_timeline->effectiveStartFrame());
    }

    m_clock.setFrameRate(m_timeline->frameRate());
    m_clock.start(m_timeline->currentFrame());
    m_tickTimer->start(tickIntervalMs());
    setState(PlaybackState::Playing);
}

void PlaybackController::pause()
{
    if (m_state != PlaybackState::Playing) {
        return;
    }
    m_clock.pause();
    m_tickTimer->stop();
    setState(PlaybackState::Paused);
}

void PlaybackController::togglePlayPause()
{
    if (m_state == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackController::stop()
{
    m_clock.stop();
    m_tickTimer->stop();
    m_timeline->setCurrentFrame(m_timeline->effectiveStartFrame());
    setState(PlaybackState::Stopped);
}

void PlaybackController::seekFrame(int64_t frame)
{
    m_timeline->setCurrentFrame(frame);
    // Re-anchor so playback continues from where the user landed instead of
    // snapping back to the pre-seek position on the next tick.
    m_clock.seekTo(m_timeline->currentFrame());
}

void PlaybackController::stepForward()
{
    // Stepping is a deliberate single-frame move, so it leaves play mode.
    if (m_state == PlaybackState::Playing) {
        pause();
    }
    seekFrame(m_timeline->currentFrame() + 1);
}

void PlaybackController::stepBackward()
{
    if (m_state == PlaybackState::Playing) {
        pause();
    }
    seekFrame(m_timeline->currentFrame() - 1);
}

void PlaybackController::goToStart()
{
    seekFrame(m_timeline->effectiveStartFrame());
}

void PlaybackController::goToEnd()
{
    seekFrame(m_timeline->effectiveEndFrame());
}

void PlaybackController::setLoopEnabled(bool enabled)
{
    if (m_loopEnabled == enabled) {
        return;
    }
    m_loopEnabled = enabled;
    qCInfo(log::playback) << "Loop" << (enabled ? "enabled" : "disabled");
    emit loopEnabledChanged(m_loopEnabled);
}

void PlaybackController::setPlaybackRange(int64_t startFrame, int64_t endFrame)
{
    timeline::PlaybackRange range;
    range.startFrame = startFrame;
    range.endFrame = endFrame;
    range.enabled = true;
    m_timeline->setPlaybackRange(range);
}

void PlaybackController::clearPlaybackRange()
{
    m_timeline->clearPlaybackRange();
}

void PlaybackController::setSpeed(double speed)
{
    m_clock.setSpeed(speed);
    if (m_tickTimer->isActive()) {
        m_tickTimer->setInterval(tickIntervalMs());
    }
}

void PlaybackController::setSource(std::shared_ptr<media::MediaSource> source)
{
    stop();
    m_source = std::move(source);

    if (m_source) {
        const media::MediaMetadata& meta = m_source->metadata();
        m_timeline->setFrameRate(meta.frameRate);
        m_timeline->setFrameCount(std::max<int64_t>(meta.effectiveFrameCount(), 0));
        qCInfo(log::playback).noquote()
            << "Source set:" << m_source->displayName()
            << "frames:" << m_timeline->frameCount();
    } else {
        m_timeline->reset();
        qCInfo(log::playback) << "Source cleared";
    }

    m_clock.setFrameRate(m_timeline->frameRate());
    emit sourceChanged();
}

void PlaybackController::onTick()
{
    const int64_t target = m_clock.currentFrame();
    const int64_t start = m_timeline->effectiveStartFrame();
    const int64_t end = m_timeline->effectiveEndFrame();

    if (m_timeline->frameCount() <= 0) {
        // No media: the transport still reports Playing so the UI state is
        // honest, but there is nothing to advance.
        return;
    }

    if (target > end) {
        if (m_loopEnabled) {
            const int64_t span = end - start + 1;
            const int64_t wrapped = span > 0 ? start + ((target - start) % span) : start;
            m_timeline->setCurrentFrame(wrapped);
            m_clock.seekTo(wrapped);
        } else {
            m_timeline->setCurrentFrame(end);
            m_clock.pause();
            m_tickTimer->stop();
            setState(PlaybackState::Paused);
            emit playbackFinished();
        }
        return;
    }

    m_timeline->setCurrentFrame(target);
}

void PlaybackController::setState(PlaybackState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;

    const char* name = state == PlaybackState::Playing ? "Playing"
                     : state == PlaybackState::Paused  ? "Paused"
                                                       : "Stopped";
    qCInfo(log::playback) << "State ->" << name;

    emit stateChanged(m_state);
}

int PlaybackController::tickIntervalMs() const
{
    const media::FrameRate rate = m_timeline->frameRate();
    if (!rate.isValid()) {
        return kIdleTickIntervalMs;
    }
    const double effectiveFps = rate.toDouble() * m_clock.speed();
    if (effectiveFps <= 0.0) {
        return kIdleTickIntervalMs;
    }
    const int interval = static_cast<int>(1000.0 / effectiveFps);
    return std::clamp(interval, 1, kMaxTickIntervalMs);
}

} // namespace atk::playback
