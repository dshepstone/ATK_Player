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

/// How far ahead of the playhead to keep decoded frames, in milliseconds.
///
/// Expressed as time rather than a frame count so the same figure means the
/// same thing at any frame rate, and so it does not silently become hundreds of
/// megabytes at 4K. Long enough to ride out a slow frame, short enough that a
/// seek throws away little work.
constexpr int64_t kLookaheadMs = 400;

/// Bounds on the derived lookahead, whatever the arithmetic produces.
constexpr int kMinLookaheadFrames = 3;
constexpr int kMaxLookaheadFrames = 24;

/// Frames to have queued before entering Playing.
///
/// Starting with an empty queue guarantees the first tick misses, which is
/// visible as a stutter at the very moment the user presses play.
constexpr int kPrerollFrames = 4;

/// How often the playback performance summary is emitted.
constexpr int64_t kPerfReportIntervalNs = 1'000'000'000;

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
    , m_generations(std::make_shared<media::DecodeGenerations>())
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

    m_worker = new media::DecoderWorker(m_audioBuffer, m_generations);
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
    connect(m_worker, &media::DecoderWorker::audioPrimed,
            this, &PlaybackController::onAudioPrimed);
    connect(this, &PlaybackController::requestLookaheadFrames,
            m_worker, &media::DecoderWorker::setLookaheadFrames);

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

    // The extent can arrive after this constructor runs -- the window installs
    // the placeholder extent immediately afterwards, and real media later still.
    // Watching the model keeps the state honest instead of freezing whatever
    // happened to be true at construction time.
    connect(m_timeline, &timeline::TimelineModel::frameCountChanged,
            this, [this](int64_t) { reconcileIdleState(); });

    m_cache.setSourceGeneration(m_generations->currentSource());
    m_queue.setSourceGeneration(m_generations->currentSource());

    reconcileIdleState();
}

PlaybackController::~PlaybackController()
{
    // Shutdown order is deliberate and must not be rearranged:
    //
    //   1. invalidate every outstanding request, so anything still in flight is
    //      already stale and long decode loops abandon their work;
    //   2. stop this side producing or consuming -- timer off, audio device
    //      stopped -- so no UI object is touched again;
    //   3. disconnect delivery, so a queued signal already posted cannot run a
    //      slot on a half-destroyed controller;
    //   4. release FFmpeg on the decode thread, synchronously;
    //   5. join the thread;
    //   6. only then let members be destroyed.
    //
    // Skipping (1) makes shutdown wait for a decode nobody wants. Skipping (3)
    // is a use-after-free waiting for the right timing.
    m_generations->bumpSource();

    haltPlaybackMachinery();

    if (m_decodeThread != nullptr) {
        m_worker->disconnect(this);
        disconnect(this, nullptr, m_worker, nullptr);

        // Blocking so FFmpeg teardown finishes on the thread that owns it
        // before that thread is asked to exit.
        QMetaObject::invokeMethod(m_worker, &media::DecoderWorker::shutdown,
                                  Qt::BlockingQueuedConnection);

        m_decodeThread->quit();
        if (!m_decodeThread->wait(5000)) {
            qCWarning(log::playback) << "Decode thread did not stop in time; terminating";
            m_decodeThread->terminate();
            m_decodeThread->wait(1000);
        }
    }
}

void PlaybackController::reconcileIdleState()
{
    const bool hasExtent = m_timeline->frameCount() > 0;

    // Only the two idle states are reconciled. Playing, Paused, Seeking,
    // Loading and Error are all mid-operation or user-visible outcomes, and
    // overwriting them from a model signal would lose information.
    if (hasExtent && m_state == PlayerState::Empty) {
        setState(PlayerState::Ready);
    } else if (!hasExtent && m_state == PlayerState::Ready) {
        setState(PlayerState::Empty);
    }
}

void PlaybackController::haltPlaybackMachinery()
{
    m_displayTimer->stop();
    emit requestStopPlayback();
    if (m_audioOutput) {
        m_audioOutput->stop();
    }
}

// ---------------------------------------------------------------------------
// Media
// ---------------------------------------------------------------------------

void PlaybackController::openMedia(const QString& filePath)
{
    qCInfo(log::playback).noquote() << "Opening media:" << filePath;

    // A new source invalidates every outstanding request and every cached
    // frame at once. Bumping first means results already in flight from the
    // previous file are stale before they can arrive.
    const quint64 sourceGeneration = m_generations->bumpSource();

    // Tear the previous session down completely before the new one starts, so
    // nothing from the old file survives into the new one.
    haltPlaybackMachinery();
    m_audioActive = false;
    m_cache.setSourceGeneration(sourceGeneration);
    m_queue.setSourceGeneration(sourceGeneration);
    m_currentFrame = media::VideoFrame{};
    m_droppedFrames = 0;
    m_errorMessage.clear();
    m_pendingSeekFrame = -1;
    m_resumeAfterSeek = false;

    setState(PlayerState::Loading);
    emit loadingChanged(true);

    emit requestOpen(filePath, sourceGeneration);
}

void PlaybackController::closeMedia()
{
    const quint64 sourceGeneration = m_generations->bumpSource();

    haltPlaybackMachinery();
    emit requestClose();

    m_audioActive = false;
    m_cache.setSourceGeneration(sourceGeneration);
    m_queue.setSourceGeneration(sourceGeneration);
    m_currentFrame = media::VideoFrame{};
    m_hasMedia = false;
    m_metadata = media::MediaMetadata{};
    m_droppedFrames = 0;

    m_timeline->reset();

    emit mediaClosed();
    setState(PlayerState::Empty);
}

void PlaybackController::onWorkerMediaOpened(const media::MediaMetadata& metadata,
                                             quint64 sourceGeneration)
{
    // A newer open superseded this one while it was being probed.
    if (!m_generations->isCurrentSource(sourceGeneration)) {
        qCDebug(log::playback) << "Ignoring open result from superseded source generation";
        return;
    }

    m_metadata = metadata;
    m_hasMedia = true;
    emit loadingChanged(false);

    // Real media replaces the placeholder extent entirely; setFrameCount()
    // clears the placeholder marking on the model.
    m_timeline->setFrameRate(metadata.frameRate);
    m_timeline->setFrameCount(std::max<int64_t>(metadata.frameCount, 0));
    m_timeline->clearPlaybackRange();
    m_timeline->setCurrentFrame(0);

    // Size the playback queue for this media. A fixed byte budget that suits
    // 1080p leaves 4K with room for only two frames of lookahead, which is not
    // enough to absorb any decode variation at all.
    if (!metadata.resolution.isEmpty()) {
        const int64_t bytesPerFrame =
            static_cast<int64_t>(metadata.resolution.width()) * metadata.resolution.height() * 4;
        const int64_t wanted = bytesPerFrame * 6;
        m_queue.setBudgetBytes(std::max(media::PlaybackQueue::kDefaultBudgetBytes, wanted));
        qCDebug(log::playback) << "Playback queue budget"
                               << (m_queue.budgetBytes() / (1024 * 1024)) << "MB";
    }

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

void PlaybackController::onWorkerMediaOpenFailed(const QString& message,
                                                 quint64 sourceGeneration)
{
    if (!m_generations->isCurrentSource(sourceGeneration)) {
        return;
    }

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

void PlaybackController::onWorkerFrameReady(const media::VideoFrame& frame,
                                            quint64 requestGeneration)
{
    if (!frame.isValid()) {
        return;
    }

    // Two independent checks, because they catch different things. The source
    // check rejects a frame from a file that is no longer open. The request
    // check rejects a frame that was correct for a seek the user has already
    // moved on from -- without it, releasing a drag can be followed by the
    // picture jumping back to an earlier position as late results land.
    if (!m_generations->isCurrentSource(frame.sourceGeneration)) {
        return;
    }
    if (!m_generations->isCurrentRequest(requestGeneration)) {
        qCDebug(log::playback) << "Dropping stale frame" << frame.frameIndex;
        return;
    }

    ++m_decodedFrames;
    m_cache.insert(frame);

    // Frames at or ahead of the playhead are what playback will need next, so
    // they also go into the queue, where eviction cannot reach them.
    if (frame.frameIndex >= m_timeline->currentFrame()) {
        m_queue.insert(frame);
    }

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

void PlaybackController::onWorkerFrameFailed(qint64 frameIndex, const QString& message,
                                             quint64 requestGeneration)
{
    if (!m_generations->isCurrentRequest(requestGeneration)) {
        // Expected whenever a request was cancelled mid-decode; not an error.
        return;
    }

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

void PlaybackController::onWorkerEndOfStream(quint64 requestGeneration)
{
    if (!m_generations->isCurrentRequest(requestGeneration)) {
        return;
    }

    // The decoder having no more frames is NOT the end of playback. On a short
    // clip the worker decodes everything within milliseconds of pressing play,
    // long before the playhead has shown any of it. Ending here would cut a
    // two-second clip off after a fraction of a second.
    //
    // Playback ends when the *playhead* reaches the end, which the display tick
    // decides. All this does is record that no further frames are coming.
    qCDebug(log::playback) << "Decoder reached end of stream";
    m_decoderAtEnd = true;
}

void PlaybackController::finishPlayback()
{
    if (m_loopEnabled) {
        qCDebug(log::playback) << "Playhead reached the end; looping";
        // Whole-clip loop for M1. Range looping is M2.
        m_decoderAtEnd = false;
    m_presentedFrames = 0;
    m_decodedFrames = 0;
    m_perfWindowStartNs = monotonicNowNs();
    m_cache.resetCounters();
        seekFrame(m_timeline->effectiveStartFrame());
        m_resumeAfterSeek = true;
        return;
    }

    qCInfo(log::playback) << "Playback reached the end";
    haltPlaybackMachinery();
    m_generations->bumpRequest();

    const int64_t last = effectiveLastFrame();
    if (last >= 0) {
        // Settle on the final frame, displaying it if it is still cached.
        if (const media::VideoFrame* cached = m_cache.find(last)) {
            presentFrame(*cached);
        } else {
            m_timeline->setCurrentFrame(last);
        }
    }
    setState(PlayerState::Ended);
}

void PlaybackController::onAudioPrimed(int bufferedMs)
{
    if (!m_audioActive || m_state != PlayerState::Playing) {
        return;
    }

    qCInfo(log::playback).noquote()
        << QStringLiteral("Starting audio output with %1 ms primed").arg(bufferedMs);

    if (bufferedMs <= 0) {
        // Nothing to play. Video continues on the monotonic clock rather than
        // waiting on audio that is not coming.
        qCWarning(log::playback)
            << "Audio could not be primed; continuing with video-only timing";
        return;
    }

    m_audioOutput->start(m_playbackStartUs);
}

void PlaybackController::onWorkerDecodeError(const QString& message)
{
    qCWarning(log::playback).noquote() << "Decode error:" << message;
    emit errorOccurred(message);
}

void PlaybackController::presentFrame(const media::VideoFrame& frame)
{
    m_currentFrame = frame;
    ++m_presentedFrames;

    // Everything before the presented frame is now past and can be released;
    // the cache still holds it for stepping backwards.
    m_queue.discardUpTo(frame.frameIndex);

    m_timeline->setCurrentFrame(frame.frameIndex);
    emit frameChanged(frame);
}

int PlaybackController::computeLookaheadFrames() const
{
    const media::FrameRate rate = effectiveFrameRate();
    if (!rate.isValid()) {
        return kMinLookaheadFrames;
    }

    // Time-based target first: the same 400 ms at any frame rate.
    const int64_t timeBased = (kLookaheadMs * rate.numerator)
                            / (1000LL * std::max(1, rate.denominator));

    // Then bound by what the queue can actually hold. A 4K frame is ~33 MB, so
    // without this the lookahead alone would ask for hundreds of megabytes and
    // the queue would spend its life discarding what it just accepted.
    int64_t byMemory = kMaxLookaheadFrames;
    const QSize resolution = m_metadata.resolution;
    if (!resolution.isEmpty()) {
        const int64_t bytesPerFrame =
            static_cast<int64_t>(resolution.width()) * resolution.height() * 4;
        if (bytesPerFrame > 0) {
            byMemory = m_queue.budgetBytes() / bytesPerFrame;
        }
    }

    const int64_t target = std::min<int64_t>(std::max<int64_t>(timeBased, 1), byMemory);
    return static_cast<int>(std::clamp<int64_t>(target, kMinLookaheadFrames, kMaxLookaheadFrames));
}

void PlaybackController::reportPerformance(bool force)
{
    const int64_t now = monotonicNowNs();
    if (m_perfWindowStartNs == 0) {
        m_perfWindowStartNs = now;
        return;
    }

    const int64_t elapsedNs = now - m_perfWindowStartNs;
    if (!force && elapsedNs < kPerfReportIntervalNs) {
        return;
    }
    if (elapsedNs <= 0) {
        return;
    }

    const double seconds = static_cast<double>(elapsedNs) / 1'000'000'000.0;
    const double presentFps = static_cast<double>(m_presentedFrames) / seconds;
    const double decodeFps = static_cast<double>(m_decodedFrames) / seconds;

    qCDebug(log::playback).noquote()
        << QStringLiteral(
               "Playback perf: present %1 fps  decode %2 fps  queue %3 frames (%4 MB)  "
               "cache %5 MB hit %6 miss %7 evict %8  audio %9 ms  underruns %10  drops %11  clock %12")
               .arg(presentFps, 0, 'f', 2)
               .arg(decodeFps, 0, 'f', 2)
               .arg(m_queue.count())
               .arg(m_queue.usedBytes() / (1024 * 1024))
               .arg(m_cache.usedBytes() / (1024 * 1024))
               .arg(m_cache.hitCount())
               .arg(m_cache.missCount())
               .arg(m_cache.evictionCount())
               .arg(m_audioOutput ? m_audioOutput->bufferedMs() : 0)
               .arg(m_audioOutput ? m_audioOutput->underrunCount() : 0)
               .arg(m_droppedFrames)
               .arg(usingAudioClock() ? QStringLiteral("audio") : QStringLiteral("monotonic"));

    m_presentedFrames = 0;
    m_decodedFrames = 0;
    m_cache.resetCounters();
    m_perfWindowStartNs = now;
}

// ---------------------------------------------------------------------------
// Clock and display
// ---------------------------------------------------------------------------

bool PlaybackController::usingAudioClock() const
{
    if (!m_audioActive || !m_audioOutput || !m_audioOutput->isOpen()) {
        return false;
    }

    // A device fed silence still advances processedUSecs(), so trusting it
    // would let a starved audio path dictate video timing while producing no
    // sound at all. Only real consumed audio earns the clock.
    return m_audioOutput->isDeliveringAudio();
}

int64_t PlaybackController::masterPositionUs() const
{
    if (usingAudioClock()) {
        const int64_t audioPosition = m_audioOutput->positionUs();
        if (audioPosition >= 0) {
            return audioPosition;
        }
        // Audio is configured but has not begun delivering yet -- the first
        // buffers are still being filled. Fall through to the monotonic clock
        // rather than stalling; audio takes over as soon as it is really
        // playing.
    }
    // No audio, or audio not yet running: a monotonic clock takes the role.
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
        finishPlayback();
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

    // Queue first: it holds the lookahead and cannot be evicted from under
    // playback. The cache is the fallback for material already played, which is
    // what a loop or a small backward jump lands on.
    if (const media::VideoFrame* queued = m_queue.find(clamped)) {
        presentFrame(*queued);
        emit requestPlayheadFrame(clamped);
        reportPerformance(false);
        return;
    }

    if (const media::VideoFrame* cached = m_cache.find(clamped)) {
        presentFrame(*cached);
        emit requestPlayheadFrame(clamped);
        reportPerformance(false);
        return;
    }

    // The frame is not decoded yet. Keeping the previous one on screen is the
    // right trade during real-time playback -- but only during playback, never
    // while stepping, where the exact frame is the whole point.
    const int64_t lateBy =
        positionUs - media::ffmpeg::frameIndexToMicroseconds(m_currentFrame.frameIndex, avRate);
    if (lateBy > kLateFrameToleranceUs) {
        ++m_droppedFrames;
    }
    emit requestPlayheadFrame(clamped);
    reportPerformance(false);
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
    m_decoderAtEnd = false;

    if (!inPlaceholderMode()) {
        // Sized from the frame rate and the decoded frame size, so 4K does not
        // ask for the same frame count as 640x360 and blow the queue budget.
        m_lookaheadFrames = computeLookaheadFrames();
        qCDebug(log::playback) << "Lookahead target" << m_lookaheadFrames << "frames";

        // Playback repositions the decoder, so it supersedes any pending seek.
        const quint64 generation = m_generations->bumpRequest();
        emit requestLookaheadFrames(m_lookaheadFrames);
        emit requestStartPlayback(from, generation);
        emit requestPlayheadFrame(from);

        // The audio device is started from onAudioPrimed(), once the worker has
        // decoded a little audio. Video begins immediately on the monotonic
        // clock and hands over to audio as soon as it is really delivering.
    }

    setState(PlayerState::Playing);
    startDisplayTimer();
}

void PlaybackController::pause()
{
    if (m_state != PlayerState::Playing) {
        return;
    }

    // Stop rather than suspend: suspending leaves the device holding audio that
    // would play on resume even after a seek elsewhere.
    haltPlaybackMachinery();

    // Pausing ends the playback run, so frames still being decoded for it are
    // no longer wanted.
    m_generations->bumpRequest();

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
    haltPlaybackMachinery();
    m_generations->bumpRequest();

    m_resumeAfterSeek = false;

    const int64_t start = m_timeline->effectiveStartFrame();

    if (inPlaceholderMode()) {
        m_timeline->setCurrentFrame(start);
        m_cache.setSourceGeneration(m_generations->currentSource());
        m_queue.setSourceGeneration(m_generations->currentSource());

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

    // Bumping here is what makes the previous in-flight decode stale: the
    // worker sees the change mid-loop and abandons it, and any result that
    // still arrives is rejected on receipt.
    emit requestFrame(target, m_generations->bumpRequest());
}

void PlaybackController::seekFrame(int64_t frame)
{
    const bool wasPlaying = m_state == PlayerState::Playing;

    if (wasPlaying) {
        haltPlaybackMachinery();
    }

    // The queue holds the future of the *old* position, so it goes. The cache
    // deliberately does not -- see below.
    m_queue.clear();

    // The cache is deliberately *not* cleared here. Its frames still belong to
    // the open source, so they stay valid across a seek -- that is what makes
    // stepping back and forth over the same few seconds instant. Correctness
    // after a seek comes from the request generation, not from discarding work.


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
