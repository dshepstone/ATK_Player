#include "media/DecoderWorker.h"

#include "audio/AudioRingBuffer.h"
#include "core/Logging.h"
#include "media/ffmpeg/FFmpegUtil.h"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace atk::media {
namespace {

/// Bytes of audio pushed per pump attempt. Small enough that a full buffer is
/// noticed quickly, large enough not to lock the mutex once per sample.
constexpr int64_t kAudioPushChunkBytes = 16 * 1024;

/// Idle wait when the decode queue is already far enough ahead.
constexpr int kIdleStepDelayMs = 4;

} // namespace

DecoderWorker::DecoderWorker(std::shared_ptr<audio::AudioRingBuffer> audioBuffer,
                             std::shared_ptr<DecodeGenerations> generations,
                             QObject* parent)
    : QObject(parent)
    , m_decoder(std::make_unique<MediaDecoder>())
    , m_audioBuffer(std::move(audioBuffer))
    , m_generations(std::move(generations))
{
    Q_ASSERT(m_generations != nullptr);
}

DecoderWorker::~DecoderWorker()
{
    // Runs on the decode thread during thread shutdown, so every FFmpeg context
    // is destroyed by the same thread that created and used it.
    m_decoder.reset();
}

bool DecoderWorker::isStale(quint64 generation) const
{
    return !m_generations->isCurrentRequest(generation);
}

void DecoderWorker::shutdown()
{
    m_shuttingDown = true;
    m_playing = false;

    if (m_audioBuffer) {
        m_audioBuffer->clear();
    }
    m_pendingAudio = AudioChunk{};
    m_pendingAudioOffset = 0;
    m_audioTrimBeforeUs = -1;
    m_positionedFrameIndex = -1;
    m_positionedFramePtsUs = -1;

    // Release FFmpeg here rather than leaving it to the destructor, so teardown
    // is an explicit step that happens before the thread is joined.
    m_decoder->close();
}

// ---------------------------------------------------------------------------
// Media lifecycle
// ---------------------------------------------------------------------------

void DecoderWorker::openMedia(const QString& filePath, quint64 sourceGeneration)
{
    if (m_shuttingDown) {
        return;
    }

    m_playing = false;
    m_reachedEnd = false;
    m_pendingAudio = AudioChunk{};
    m_pendingAudioOffset = 0;
    m_audioTrimBeforeUs = -1;
    m_positionedFrameIndex = -1;
    m_positionedFramePtsUs = -1;
    m_decodedAheadTo = -1;

    if (m_audioBuffer) {
        m_audioBuffer->clear();
    }

    // Closing first releases the previous file's contexts before the new ones
    // are allocated, so two decoders never exist at once.
    m_decoder->close();

    QString error;
    if (!m_decoder->open(filePath, &error)) {
        m_decoder->close();
        emit mediaOpenFailed(error, sourceGeneration);
        return;
    }

    // Another open may have been requested while this one was running.
    if (!m_generations->isCurrentSource(sourceGeneration)) {
        qCDebug(log::media) << "Discarding open of" << filePath << "- superseded";
        m_decoder->close();
        return;
    }

    MediaMetadata metadata = m_decoder->metadata();

    // A container frame count is often absent or wrong. For short clips the
    // truth is cheap to establish by decoding, and being exact matters: the
    // status bar, End, and the timeline extent all depend on it.
    if (!metadata.hasExactFrameCount()) {
        QString countError;
        const int64_t counted = m_decoder->countFramesExactly(&countError);
        if (counted > 0) {
            metadata.frameCount = counted;
            metadata.frameCountSource = FrameCountSource::Counted;
            qCInfo(log::media) << "Counted" << counted << "frames exactly";
        } else {
            qCInfo(log::media) << "Using estimated frame count" << metadata.frameCount;
        }
    }

    if (!m_generations->isCurrentSource(sourceGeneration)) {
        m_decoder->close();
        return;
    }

    emit mediaOpened(metadata, sourceGeneration);

    // Show something immediately rather than waiting for the user to press play.
    requestFrame(0, m_generations->currentRequest());
}

void DecoderWorker::closeMedia()
{
    m_playing = false;
    m_reachedEnd = false;
    m_pendingAudio = AudioChunk{};
    m_pendingAudioOffset = 0;
    m_decodedAheadTo = -1;

    if (m_audioBuffer) {
        m_audioBuffer->clear();
    }

    m_decoder->close();
    emit mediaClosed();
}

void DecoderWorker::configureAudio(int sampleRate, int channelCount)
{
    if (!m_decoder->isOpen() || !m_decoder->metadata().hasAudio) {
        return;
    }

    AudioFormat format;
    format.sampleRate = sampleRate;
    format.channelCount = channelCount;
    format.bytesPerSample = 2; // signed 16-bit interleaved

    QString error;
    if (!m_decoder->configureAudioOutput(format, &error)) {
        qCWarning(log::media).noquote() << "Audio output configuration failed:" << error;
        emit decodeError(error);
        return;
    }

    if (m_audioBuffer) {
        m_audioBuffer->setBytesPerSecond(format.bytesPerSecond());
    }

    // Media open displays frame zero before the UI can report the audio
    // device's accepted format. Packets demuxed while producing that first
    // picture could not be resampled and were intentionally discarded. Seek
    // back to the exact displayed epoch now that audio is configured, or the
    // first playback run starts with a shortened audio stream and its master
    // clock stalls before the final video frames/loop boundary.
    if (m_positionedFrameIndex >= 0) {
        QString seekError;
        if (!m_decoder->seekToFrameIndex(m_positionedFrameIndex, &seekError)) {
            qCWarning(log::media).noquote()
                << "Could not restore decoder after audio configuration:" << seekError;
            emit decodeError(seekError);
            return;
        }
        m_audioTrimBeforeUs = m_positionedFramePtsUs;
        m_pendingAudio = AudioChunk{};
        m_pendingAudioOffset = 0;
    }
}

// ---------------------------------------------------------------------------
// Frame requests
// ---------------------------------------------------------------------------

void DecoderWorker::requestFrame(qint64 frameIndex, quint64 requestGeneration)
{
    if (m_shuttingDown || !m_decoder->isOpen()) {
        if (!m_shuttingDown) {
            emit frameFailed(frameIndex, QStringLiteral("No media is open."), requestGeneration);
        }
        return;
    }

    // Superseded before the request was even picked up.
    if (isStale(requestGeneration)) {
        qCDebug(log::media) << "Skipping stale frame request" << frameIndex;
        return;
    }

    // A frame request means the playhead moved, so anything buffered for audio
    // belongs to the old position.
    if (m_audioBuffer) {
        m_audioBuffer->clear();
    }
    m_pendingAudio = AudioChunk{};
    m_pendingAudioOffset = 0;

    VideoFrame frame;
    QString error;

    // Decoding forward from a keyframe can take a while; abandon it as soon as
    // a newer request arrives rather than finishing work nobody wants.
    const bool decoded = m_decoder->frameAtIndex(
        frameIndex, frame, &error,
        [this, requestGeneration] { return isStale(requestGeneration); });

    if (isStale(requestGeneration)) {
        qCDebug(log::media) << "Discarding frame" << frameIndex << "- superseded during decode";
        return;
    }

    if (!decoded) {
        qCWarning(log::media).noquote()
            << "Frame" << frameIndex << "could not be decoded:" << error;
        emit frameFailed(frameIndex, error, requestGeneration);
        return;
    }

    frame.sourceGeneration = m_generations->currentSource();
    m_positionedFrameIndex = frame.frameIndex;
    m_positionedFramePtsUs = frame.ptsUs;
    m_audioTrimBeforeUs = frame.ptsUs;

    m_reachedEnd = false;
    m_decodedAheadTo = frame.frameIndex;
    m_playheadFrame.store(frame.frameIndex);

    emit frameReady(frame, requestGeneration);
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void DecoderWorker::startPlayback(qint64 fromFrameIndex, quint64 requestGeneration)
{
    if (m_shuttingDown || !m_decoder->isOpen() || isStale(requestGeneration)) {
        return;
    }

    // Only reposition when the decoder is not already sitting at the right
    // place; a needless seek at the start of every play would drop the frames
    // already decoded ahead.
    if (m_positionedFrameIndex != fromFrameIndex) {
        QString error;
        if (!m_decoder->seekToFrameIndex(fromFrameIndex, &error)) {
            emit decodeError(error);
            return;
        }
        if (m_audioBuffer) {
            m_audioBuffer->clear();
        }
        m_pendingAudio = AudioChunk{};
        m_pendingAudioOffset = 0;
        m_audioTrimBeforeUs = -1;
        m_positionedFramePtsUs = -1;
    }

    const int64_t playbackOriginUs = m_positionedFramePtsUs >= 0
        ? m_positionedFramePtsUs
        : ffmpeg::frameIndexToMicroseconds(
              fromFrameIndex,
              AVRational{ m_decoder->metadata().frameRate.numerator,
                          m_decoder->metadata().frameRate.denominator });

    // Preroll, before the run starts and before the device is told to play.
    // Starting QAudioSink against an empty buffer means it immediately pulls
    // silence and the audio clock cannot begin, which is what made the first
    // moments of playback silent.
    pumpAudioUpTo(kAudioTargetMs, kMaxAudioBytesPerStep * 16);
    const int64_t primedMs = bufferedAudioMs();
    if (m_decoder->metadata().hasAudio) {
        qCDebug(log::media) << "Audio preroll:" << primedMs << "ms";
    }

    m_playing = true;
    m_reachedEnd = false;
    m_playbackGeneration = requestGeneration;
    m_playheadFrame.store(fromFrameIndex);
    m_decodedAheadTo = fromFrameIndex - 1;

    // Tell the controller audio is ready, so it starts the device against a
    // primed buffer rather than against silence.
    emit audioPrimed(static_cast<int>(primedMs), playbackOriginUs, requestGeneration);

    scheduleNextStep();
}

void DecoderWorker::stopPlayback()
{
    m_playing = false;
}

void DecoderWorker::setPlayheadFrame(qint64 frameIndex)
{
    m_playheadFrame.store(frameIndex);

    // Decoding may have paused because the queue was far enough ahead; the
    // playhead advancing is what makes more work worth doing.
    if (m_playing) {
        scheduleNextStep();
    }
}

void DecoderWorker::scheduleNextStep()
{
    if (m_stepScheduled || !m_playing || m_shuttingDown) {
        return;
    }
    m_stepScheduled = true;

    // Queued rather than a direct loop: any seek, pause or close the UI has
    // already posted is processed before the next unit of decoding.
    QMetaObject::invokeMethod(this, &DecoderWorker::decodeStep, Qt::QueuedConnection);
}

void DecoderWorker::decodeStep()
{
    m_stepScheduled = false;

    if (!m_playing || m_shuttingDown || !m_decoder->isOpen()) {
        return;
    }

    // A seek during playback supersedes this run; the controller restarts it.
    if (isStale(m_playbackGeneration)) {
        m_playing = false;
        return;
    }

    pumpAudio();

    const int64_t playhead = m_playheadFrame.load();
    const bool farEnoughAhead = m_decodedAheadTo >= playhead + m_lookaheadFrames;

    if (!farEnoughAhead && !m_reachedEnd) {
        VideoFrame frame;
        QString error;
        const DecodeStatus status = m_decoder->nextVideoFrame(frame, &error);

        if (status == DecodeStatus::Error) {
            qCWarning(log::media).noquote() << "Decode error during playback:" << error;
            m_playing = false;
            emit decodeError(error);
            return;
        }
        if (status == DecodeStatus::EndOfFile) {
            m_reachedEnd = true;
            emit endOfStream(m_playbackGeneration);
        } else {
            frame.sourceGeneration = m_generations->currentSource();
            m_decodedAheadTo = frame.frameIndex;
            emit frameReady(frame, m_playbackGeneration);
        }
    }

    if (!m_playing) {
        return;
    }

    if (farEnoughAhead || m_reachedEnd) {
        // Nothing useful to do right now. Come back shortly rather than
        // spinning the event loop at full speed.
        m_stepScheduled = true;
        QTimer::singleShot(kIdleStepDelayMs, this, [this] {
            m_stepScheduled = false;
            scheduleNextStep();
        });
        return;
    }

    scheduleNextStep();
}

int64_t DecoderWorker::bufferedAudioMs() const
{
    if (!m_audioBuffer) {
        return 0;
    }
    const AudioFormat format = m_decoder->outputAudioFormat();
    if (!format.isValid()) {
        return 0;
    }
    return format.bytesToMicroseconds(m_audioBuffer->bytesAvailable()) / 1000;
}

void DecoderWorker::pumpAudio()
{
    pumpAudioUpTo(kAudioTargetMs, kMaxAudioBytesPerStep);
}

void DecoderWorker::pumpAudioUpTo(int64_t targetMs, int64_t maxBytesThisCall)
{
    if (!m_audioBuffer || !m_decoder->isOpen() || !m_decoder->metadata().hasAudio) {
        return;
    }
    if (!m_decoder->outputAudioFormat().isValid()) {
        return;
    }

    // Already have enough queued: leave the thread free for video.
    if (bufferedAudioMs() >= targetMs) {
        return;
    }

    int64_t writtenThisCall = 0;

    // Two bounds, both necessary. The target stops audio running arbitrarily
    // far ahead of video; the byte cap stops a single call monopolising the
    // decode thread when the buffer starts far below target. Without them, one
    // pumpAudio() could decode seconds of audio -- and demux all the video
    // packets interleaved with it -- before video decoding got a turn.
    while (writtenThisCall < maxBytesThisCall) {
        if (m_pendingAudio.pcm.isEmpty()) {
            AudioChunk chunk;
            QString error;
            const DecodeStatus status = m_decoder->nextAudioChunk(chunk, &error);
            if (status != DecodeStatus::Ok) {
                return;
            }
            m_pendingAudio = std::move(chunk);
            m_pendingAudioOffset = 0;
        }

        if (!preparePendingAudioForEpoch()) {
            continue;
        }

        const int64_t remaining = m_pendingAudio.pcm.size() - m_pendingAudioOffset;
        if (remaining <= 0) {
            m_pendingAudio = AudioChunk{};
            m_pendingAudioOffset = 0;
            continue;
        }

        const int64_t attempt = std::min(remaining, kAudioPushChunkBytes);
        const int64_t consumedUs =
            m_decoder->outputAudioFormat().bytesToMicroseconds(m_pendingAudioOffset);

        const int64_t written = m_audioBuffer->write(
            m_pendingAudio.pcm.constData() + m_pendingAudioOffset,
            attempt,
            m_pendingAudio.ptsUs + consumedUs);

        if (written <= 0) {
            // Buffer full: leave the rest for the next pump.
            return;
        }

        m_pendingAudioOffset += written;
        writtenThisCall += written;

        if (m_pendingAudioOffset >= m_pendingAudio.pcm.size()) {
            m_pendingAudio = AudioChunk{};
            m_pendingAudioOffset = 0;
        }

        // Reached the target mid-call: stop here rather than running to the
        // byte cap, so audio never drifts arbitrarily far ahead of video.
        if (bufferedAudioMs() >= targetMs) {
            return;
        }
    }
}

bool DecoderWorker::preparePendingAudioForEpoch()
{
    if (m_audioTrimBeforeUs < 0 || m_pendingAudio.pcm.isEmpty()) {
        return true;
    }

    const int64_t beforePts = m_pendingAudio.ptsUs;
    const int64_t trimmed = trimAudioChunkBefore(
        m_pendingAudio, m_decoder->outputAudioFormat(), m_audioTrimBeforeUs);
    m_pendingAudioOffset = 0;
    if (trimmed > 0) {
        qCDebug(log::media) << "Audio seek trim requested" << m_audioTrimBeforeUs
                            << "chunk" << beforePts << "trimmed bytes" << trimmed
                            << "retained pts" << m_pendingAudio.ptsUs;
    }
    if (m_pendingAudio.pcm.isEmpty()) {
        return false;
    }
    m_audioTrimBeforeUs = -1;
    return true;
}

void DecoderWorker::setLookaheadFrames(int frames)
{
    m_lookaheadFrames = std::clamp(frames, 1, 240);
}

void DecoderWorker::primeAudio(int targetMs)
{
    if (!m_decoder->isOpen() || !m_decoder->metadata().hasAudio
        || !m_decoder->outputAudioFormat().isValid()) {
        emit audioPrimed(0, -1, m_generations->currentRequest());
        return;
    }

    // Starting QAudioSink against an empty buffer means the device immediately
    // pulls silence, and the audio clock cannot start until real bytes are
    // consumed. Filling a little first makes the first sound heard the actual
    // first sound of the clip.
    //
    // The byte cap is generous here because this runs once, before playback,
    // rather than inside the decode loop.
    pumpAudioUpTo(targetMs, kMaxAudioBytesPerStep * 16);

    const int64_t buffered = bufferedAudioMs();
    qCDebug(log::media) << "Audio primed to" << buffered << "ms (target" << targetMs << "ms)";
    emit audioPrimed(static_cast<int>(buffered), m_positionedFramePtsUs,
                     m_generations->currentRequest());
}

} // namespace atk::media
