#include "playback/PlaybackController.h"

#include "audio/AudioOutput.h"
#include "audio/AudioRingBuffer.h"
#include "core/Logging.h"
#include "media/DecoderWorker.h"
#include "media/ffmpeg/FFmpegUtil.h"
#include "timeline/TimelineModel.h"

#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace atk::playback {
namespace {

/// Display tick when no frame rate is known.
constexpr int kIdleDisplayIntervalMs = 33;

/// Never tick slower than this, so scrubbing stays responsive on low-rate media.
constexpr int kMaxDisplayIntervalMs = 40;

/// A frame this far past its presentation time during playback is not worth
/// showing -- the next one is already due.
constexpr int64_t kLateFrameToleranceUs = 100'000;

int64_t monotonicNowNs()
{
    static QElapsedTimer timer;
    if (!timer.isValid()) {
        timer.start();
    }
    return timer.nsecsElapsed();
}

const char* stateName(PlayerState state)
{
    switch (state) {
    case PlayerState::Empty:   return "Empty";
    case PlayerState::Loading: return "Loading";
    case PlayerState::Ready:   return "Ready";
    case PlayerState::Playing: return "Playing";
    case PlayerState::Paused:  return "Paused";
    case PlayerState::Seeking: return "Seeking";
    case PlayerState::Ended:   return "Ended";
    case PlayerState::Error:   return "Error";
    }
    return "Unknown";
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PlaybackController::PlaybackController(timeline::TimelineModel* timeline, QObject* parent)
    : QObject(parent)
    , m_timeline(timeline)
    , m_audioBuffer(std::make_shared<audio::AudioRingBuffer>())
    , m_displayTimer(new QTimer(this))
{
    Q_ASSERT(m_timeline != nullptr);

    qRegisterMetaType<media::MediaMetadata>("atk::media::MediaMetadata");
    qRegisterMetaType<media::VideoFrame>("atk::media::VideoFrame");
    qRegisterMetaType<PlayerState>("atk::playback::PlayerState");

    m_audioOutput = std::make_unique<audio::AudioOutput>(m_audioBuffer, this);

    m_displayTimer->setTimerType(Qt::PreciseTimer);
    connect(m_displayTimer, &QTimer::timeout, this, &PlaybackController::onDisplayTick);

    // --- Decode thread ----------------------------------------------------
    m_decodeThread = new QThread(this);
    m_decodeThread->setObjectName(QStringLiteral("ATK decode"));

    m_worker = new media::DecoderWorker(m_audioBuffer);
    m_worker->moveToThread(m_decodeThread);

    // The worker is destroyed by its own thread, so the FFmpeg contexts it owns
    // are released by the thread that used them.
    connect(m_decodeThread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Requests out. Queued because the worker lives on another thread.
    connect(this, &PlaybackController::requestOpen,
            m_worker, &media::DecoderWorker::openMedia);
    connect(this, &PlaybackController::requestClose,
            m_worker, &media::DecoderWorker::closeMedia);
    connect(this, &PlaybackController::requestFrame,
            m_worker, &media::DecoderWorker::requestFrame);
    connect(this, &PlaybackController::requestStartPlayback,
            m_worker, &media::DecoderWorker::startPlayback);
    connect(this, &PlaybackController::requestStopPlayback,
            m_worker, &media::DecoderWorker::stopPlayback);
    connect(this, &PlaybackController::requestPlayheadFrame,
            m_worker, &media::DecoderWorker::setPlayheadFrame);
    connect(this, &PlaybackController::requestConfigureAudio,
            m_worker, &media::DecoderWorker::configureAudio);

    // Results back.
    connect(m_worker, &media::DecoderWorker::mediaOpened,
            this, &PlaybackController::onWorkerMediaOpened);
    connect(m_worker, &media::DecoderWorker::mediaOpenFailed,
            this, &PlaybackController::onWorkerMediaOpenFailed);
    connect(m_worker, &media::DecoderWorker::frameReady,
            this, &PlaybackController::onWorkerFrameReady);
    connect(m_worker, &media::DecoderWorker::frameFailed,
            this, &PlaybackController::onWorkerFrameFailed);
    connect(m_worker, &media::DecoderWorker::endOfStream,
            this, &PlaybackController::onWorkerEndOfStream);
    connect(m_worker, &media::DecoderWorker::decodeError,
            this, &PlaybackController::onWorkerDecodeError);

    connect(m_audioOutput.get(), &audio::AudioOutput::deviceError,
            this, [this](const QString& message) {
                qCWarning(log::playback).noquote() << message;
                emit errorOccurred(message);
            });

    m_decodeThread->start();

    // Keep the frame rate the display timer paces against current.
    connect(m_timeline, &timeline::TimelineModel::frameRateChanged,
            this, [this](media::FrameRate) {
                if (m_displayTimer->isActive()) {
                    m_displayTimer->setInterval(displayIntervalMs());
                }
            });

    setState(m_timeline->frameCount() > 0 ? PlayerState::Ready : PlayerState::Empty);
}

PlaybackController::~PlaybackController()
{
    // Stop producing before tearing anything down, or the worker could emit
    // into a half-destroyed controller.
    m_displayTimer->stop();
    if (m_audioOutput) {
        m_audioOutput->stop();
    }

    if (m_decodeThread != nullptr) {
        // Disconnect first: queued signals already in flight must not run
        // against a controller that is being destroyed.
        m_worker->disconnect(this);
        disconnect(this, nullptr, m_worker, nullptr);

        QMetaObject::invokeMethod(m_worker, &media::DecoderWorker::closeMedia,
                                  Qt::BlockingQueuedConnection);

        m_decodeThread->quit();
        if (!m_decodeThread->wait(5000)) {
            qCWarning(log::playback) << "Decode thread did not stop in time; terminating";
            m_decodeThread->terminate();
            m_decodeThread->wait(1000);
        }
    }
}

// ---------------------------------------------------------------------------
// Media
// ---------------------------------------------------------------------------

void PlaybackController::openMedia(const QString& filePath)
{
    qCInfo(log::playback).noquote() << "Opening media:" << filePath;

    // Tear the previous session down completely before the new one starts, so
    // nothing from the old file survives into the new one.
    m_displayTimer->stop();
    emit requestStopPlayback();
    m_audioOutput->stop();
    m_audioActive = false;
    m_cache.clear();
    m_currentFrame = media::VideoFrame{};
    m_droppedFrames = 0;
    m_errorMessage.clear();
    m_pendingSeekFrame = -1;
    m_resumeAfterSeek = false;

    setState(PlayerState::Loading);
    emit loadingChanged(true);

    emit requestOpen(filePath);
}

void PlaybackController::closeMedia()
{
    m_displayTimer->stop();
    emit requestStopPlayback();
    emit requestClose();

    m_audioOutput->stop();
    m_audioActive = false;
    m_cache.clear();
    m_currentFrame = media::VideoFrame{};
    m_hasMedia = false;
    m_metadata = media::MediaMetadata{};
    m_droppedFrames = 0;

    m_timeline->reset();

    emit mediaClosed();
    setState(PlayerState::Empty);
}

void PlaybackController::onWorkerMediaOpened(const media::MediaMetadata& metadata)
{
    m_metadata = metadata;
    m_hasMedia = true;
    emit loadingChanged(false);

    // Real media replaces the placeholder extent entirely; setFrameCount()
    // clears the placeholder marking on the model.
    m_timeline->setFrameRate(metadata.frameRate);
    m_timeline->setFrameCount(std::max<int64_t>(metadata.frameCount, 0));
    m_timeline->clearPlaybackRange();
    m_timeline->setCurrentFrame(0);

    // Open the device now so the decoder can be told the exact format to
    // resample to. A machine with no audio device simply plays video.
    if (metadata.hasAudio) {
        if (m_audioOutput->open(metadata.audioSampleRate, metadata.audioChannelCount)) {
            const media::AudioFormat format = m_audioOutput->actualFormat();
            m_audioActive = true;
            emit requestConfigureAudio(format.sampleRate, format.channelCount);
        } else {
            m_audioActive = false;
            qCInfo(log::playback) << "Playing without audio: no usable output device";
        }
    } else {
        m_audioActive = false;
    }

    qCInfo(log::playback).noquote()
        << "Media ready:" << metadata.fileName
        << "frames" << metadata.frameCount
        << (metadata.hasExactFrameCount() ? "(exact)" : "(estimated)")
        << "duration" << metadata.durationSeconds() << "s";

    setState(PlayerState::Ready);
    emit mediaOpened(metadata);
}

void PlaybackController::onWorkerMediaOpenFailed(const QString& message)
{
    emit loadingChanged(false);
    m_hasMedia = false;
    m_metadata = media::MediaMetadata{};
    m_currentFrame = media::VideoFrame{};
    m_cache.clear();
    m_timeline->reset();
    setError(message);
}

// ---------------------------------------------------------------------------
// Frame delivery
// ---------------------------------------------------------------------------

void PlaybackController::onWorkerFrameReady(const media::VideoFrame& frame)
{
    if (!frame.isValid()) {
        return;
    }

    m_cache.insert(frame);

    // A frame arriving for the position a seek asked for completes that seek.
    if (m_pendingSeekFrame >= 0 && frame.frameIndex == m_pendingSeekFrame) {
        m_pendingSeekFrame = -1;
        presentFrame(frame);

        if (m_resumeAfterSeek) {
            m_resumeAfterSeek = false;
            play();
        } else if (m_state == PlayerState::Seeking) {
            setState(PlayerState::Ready);
        }
        return;
    }

    // Outside playback, the first frame of a newly opened file is what the
    // viewer should show.
    if (m_state == PlayerState::Ready && !m_currentFrame.isValid()) {
        presentFrame(frame);
    }
}

void PlaybackController::onWorkerFrameFailed(qint64 frameIndex, const QString& message)
{
    qCWarning(log::playback).noquote()
        << "Frame" << frameIndex << "failed:" << message;

    if (m_pendingSeekFrame == frameIndex) {
        m_pendingSeekFrame = -1;
        m_resumeAfterSeek = false;
        if (m_state == PlayerState::Seeking) {
            setState(PlayerState::Ready);
        }
    }
}

void PlaybackController::onWorkerEndOfStream()
{
    if (m_state != PlayerState::Playing) {
        return;
    }

    if (m_loopEnabled) {
        qCDebug(log::playback) << "End of stream reached; looping";
        // Whole-clip loop for M1. Range looping is M2.
        seekFrame(0);
        m_resumeAfterSeek = true;
        return;
    }

    qCInfo(log::playback) << "End of stream reached";
    m_displayTimer->stop();
    emit requestStopPlayback();
    m_audioOutput->stop();

    const int64_t last = effectiveLastFrame();
    if (last >= 0) {
        m_timeline->setCurrentFrame(last);
    }
    setState(PlayerState::Ended);
}

void PlaybackController::onWorkerDecodeError(const QString& message)
{
    qCWarning(log::playback).noquote() << "Decode error:" << message;
    emit errorOccurred(message);
}

void PlaybackController::presentFrame(const media::VideoFrame& frame)
{
    m_currentFrame = frame;
    m_timeline->setCurrentFrame(frame.frameIndex);
    emit frameChanged(frame);
}

// ---------------------------------------------------------------------------
// Clock and display
// ---------------------------------------------------------------------------

int64_t PlaybackController::masterPositionUs() const
{
    if (m_audioActive && m_audioOutput->isOpen()) {
        const int64_t audioPosition = m_audioOutput->positionUs();
        if (audioPosition >= 0) {
            return audioPosition;
        }
    }
    // No audio: a monotonic clock takes the master role.
    return m_playbackStartUs + (monotonicNowNs() - m_monotonicStartNs) / 1000;
}

media::FrameRate PlaybackController::effectiveFrameRate() const
{
    return m_timeline->frameRate();
}

int64_t PlaybackController::effectiveLastFrame() const
{
    return m_timeline->effectiveEndFrame();
}

int PlaybackController::displayIntervalMs() const
{
    const media::FrameRate rate = effectiveFrameRate();
    if (!rate.isValid()) {
        return kIdleDisplayIntervalMs;
    }
    // Tick about twice per frame so a frame is picked up close to its due time
    // rather than up to a whole frame late.
    const double fps = rate.toDouble();
    if (fps <= 0.0) {
        return kIdleDisplayIntervalMs;
    }
    const int interval = static_cast<int>(500.0 / fps);
    return std::clamp(interval, 1, kMaxDisplayIntervalMs);
}

void PlaybackController::startDisplayTimer()
{
    m_displayTimer->start(displayIntervalMs());
}

void PlaybackController::stopDisplayTimer()
{
    m_displayTimer->stop();
}

void PlaybackController::onDisplayTick()
{
    if (m_state != PlayerState::Playing) {
        return;
    }

    const media::FrameRate rate = effectiveFrameRate();
    if (!rate.isValid()) {
        return;
    }

    const int64_t positionUs = masterPositionUs();
    const AVRational avRate{ rate.numerator, rate.denominator };
    const int64_t targetFrame = media::ffmpeg::microsecondsToFrameIndex(positionUs, avRate);

    const int64_t start = m_timeline->effectiveStartFrame();
    const int64_t end = effectiveLastFrame();

    if (end >= 0 && targetFrame > end) {
        onWorkerEndOfStream();
        return;
    }

    const int64_t clamped = std::clamp(targetFrame, start, end < 0 ? targetFrame : end);

    if (inPlaceholderMode()) {
        // No decoder involved: the placeholder extent is driven straight from
        // the clock, exactly as it was before there was a decoder.
        if (clamped != m_timeline->currentFrame()) {
            m_timeline->setCurrentFrame(clamped);
        }
        return;
    }

    if (clamped == m_currentFrame.frameIndex) {
        return;
    }

    const media::VideoFrame* cached = m_cache.find(clamped);
    if (cached != nullptr) {
        presentFrame(*cached);
        emit requestPlayheadFrame(clamped);
        return;
    }

    // The frame is not decoded yet. Keeping the previous one on screen is the
    // right trade during real-time playback -- but only during playback, never
    // while stepping, where the exact frame is the whole point.
    const int64_t lateBy =
        positionUs - media::ffmpeg::frameIndexToMicroseconds(m_currentFrame.frameIndex, avRate);
    if (lateBy > kLateFrameToleranceUs) {
        ++m_droppedFrames;
        if (m_droppedFrames % 30 == 1) {
            qCDebug(log::playback)
                << "Dropped frame; decoder behind by" << lateBy << "us, total drops"
                << m_droppedFrames;
        }
    }
    emit requestPlayheadFrame(clamped);
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void PlaybackController::play()
{
    if (m_state == PlayerState::Playing || m_state == PlayerState::Error) {
        return;
    }
    if (m_timeline->frameCount() <= 0) {
        return;
    }

    int64_t from = m_timeline->currentFrame();

    // Playing from the very end restarts rather than playing nothing.
    if ((m_state == PlayerState::Ended || from >= effectiveLastFrame())
        && effectiveLastFrame() > 0) {
        from = m_timeline->effectiveStartFrame();
        m_timeline->setCurrentFrame(from);
        m_cache.clear();
    }

    const media::FrameRate rate = effectiveFrameRate();
    const AVRational avRate{ rate.numerator, rate.denominator };
    m_playbackStartUs = media::ffmpeg::frameIndexToMicroseconds(from, avRate);
    m_monotonicStartNs = monotonicNowNs();

    if (!inPlaceholderMode()) {
        emit requestStartPlayback(from);
        emit requestPlayheadFrame(from);

        if (m_audioActive) {
            m_audioOutput->start(m_playbackStartUs);
        }
    }

    setState(PlayerState::Playing);
    startDisplayTimer();
}

void PlaybackController::pause()
{
    if (m_state != PlayerState::Playing) {
        return;
    }

    stopDisplayTimer();
    emit requestStopPlayback();

    // Stop rather than suspend: suspending leaves the device holding audio that
    // would play on resume even after a seek elsewhere.
    if (m_audioActive) {
        m_audioOutput->stop();
    }

    setState(PlayerState::Paused);
}

void PlaybackController::togglePlayPause()
{
    if (m_state == PlayerState::Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackController::stop()
{
    stopDisplayTimer();
    emit requestStopPlayback();

    if (m_audioActive) {
        m_audioOutput->stop();
    }

    m_resumeAfterSeek = false;

    const int64_t start = m_timeline->effectiveStartFrame();

    if (inPlaceholderMode()) {
        m_timeline->setCurrentFrame(start);
        setState(m_timeline->frameCount() > 0 ? PlayerState::Ready : PlayerState::Empty);
        return;
    }

    // Stop returns to the first frame and shows it, but keeps the media loaded.
    setState(PlayerState::Ready);
    seekAndShow(start, false);
}

void PlaybackController::seekAndShow(int64_t frame, bool keepPlaying)
{
    const int64_t start = m_timeline->effectiveStartFrame();
    const int64_t end = effectiveLastFrame();
    const int64_t target = std::clamp(frame, start, end < 0 ? frame : end);

    if (inPlaceholderMode()) {
        m_timeline->setCurrentFrame(target);
        return;
    }

    // A cached frame makes stepping instant, which is what the cache is for:
    // review scrubs back and forth over the same few seconds constantly.
    const media::VideoFrame* cached = m_cache.find(target);
    if (cached != nullptr && !keepPlaying) {
        presentFrame(*cached);
        emit requestPlayheadFrame(target);
        if (m_state == PlayerState::Seeking) {
            setState(PlayerState::Ready);
        }
        return;
    }

    m_pendingSeekFrame = target;
    m_resumeAfterSeek = keepPlaying;

    if (m_state != PlayerState::Seeking) {
        setState(PlayerState::Seeking);
    }
    emit requestFrame(target);
}

void PlaybackController::seekFrame(int64_t frame)
{
    const bool wasPlaying = m_state == PlayerState::Playing;

    if (wasPlaying) {
        stopDisplayTimer();
        emit requestStopPlayback();
        if (m_audioActive) {
            m_audioOutput->stop();
        }
    }

    // Frames either side of the old position are no longer the ones wanted, and
    // keeping them would let a stale frame be displayed after the seek.
    if (!inPlaceholderMode()) {
        m_cache.clear();
    }

    seekAndShow(frame, wasPlaying);
}

void PlaybackController::stepForward()
{
    // Stepping is a deliberate single-frame move, so it leaves play mode --
    // and, per M1 scope, audio does not follow it.
    if (m_state == PlayerState::Playing) {
        pause();
    }
    const int64_t target = m_timeline->currentFrame() + 1;
    if (target > effectiveLastFrame() && effectiveLastFrame() >= 0) {
        return;
    }
    seekAndShow(target, false);
}

void PlaybackController::stepBackward()
{
    if (m_state == PlayerState::Playing) {
        pause();
    }
    const int64_t target = m_timeline->currentFrame() - 1;
    if (target < m_timeline->effectiveStartFrame()) {
        return;
    }
    seekAndShow(target, false);
}

void PlaybackController::goToStart()
{
    seekFrame(m_timeline->effectiveStartFrame());
}

void PlaybackController::goToEnd()
{
    seekFrame(effectiveLastFrame());
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

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

void PlaybackController::setMuted(bool muted)
{
    m_audioOutput->setMuted(muted);
}

bool PlaybackController::isMuted() const
{
    return m_audioOutput->isMuted();
}

void PlaybackController::setVolume(qreal volume)
{
    m_audioOutput->setVolume(volume);
}

qreal PlaybackController::volume() const
{
    return m_audioOutput->volume();
}

bool PlaybackController::hasAudioOutput() const
{
    return m_audioActive;
}

int64_t PlaybackController::currentFrame() const
{
    return m_timeline->currentFrame();
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void PlaybackController::setState(PlayerState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    qCInfo(log::playback) << "State ->" << stateName(state);
    emit stateChanged(m_state);
}

void PlaybackController::setError(const QString& message)
{
    m_errorMessage = message;
    qCWarning(log::playback).noquote() << "Error:" << message;
    setState(PlayerState::Error);
    emit errorOccurred(message);
}

} // namespace atk::playback
