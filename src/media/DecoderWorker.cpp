#include "media/DecoderWorker.h"

#include "audio/AudioRingBuffer.h"
#include "core/Logging.h"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace atk::media {
namespace {

/// Bytes of audio pushed per pump attempt. Small enough that a full buffer is
/// noticed quickly, large enough not to lock the mutex once per sample.
constexpr int64_t kAudioPushChunkBytes = 16 * 1024;

} // namespace

DecoderWorker::DecoderWorker(std::shared_ptr<audio::AudioRingBuffer> audioBuffer,
                             QObject* parent)
    : QObject(parent)
    , m_decoder(std::make_unique<MediaDecoder>())
    , m_audioBuffer(std::move(audioBuffer))
{
}

DecoderWorker::~DecoderWorker()
{
    // Runs on the decode thread during thread shutdown, so the decoder is
    // destroyed by the same thread that used it.
    m_decoder.reset();
}

void DecoderWorker::openMedia(const QString& filePath)
{
    m_playing = false;
    m_reachedEnd = false;
    m_pendingAudio = AudioChunk{};
    m_pendingAudioOffset = 0;
    m_decodedAheadTo = -1;

    if (m_audioBuffer) {
        m_audioBuffer->clear();
    }

    QString error;
    if (!m_decoder->open(filePath, &error)) {
        m_decoder->close();
        emit mediaOpenFailed(error);
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

    emit mediaOpened(metadata);

    // Show something immediately rather than waiting for the user to press play.
    requestFrame(0);
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
}

void DecoderWorker::requestFrame(qint64 frameIndex)
{
    if (!m_decoder->isOpen()) {
        emit frameFailed(frameIndex, QStringLiteral("No media is open."));
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
    if (!m_decoder->frameAtIndex(frameIndex, frame, &error)) {
        qCWarning(log::media).noquote()
            << "Frame" << frameIndex << "could not be decoded:" << error;
        emit frameFailed(frameIndex, error);
        return;
    }

    m_reachedEnd = false;
    m_decodedAheadTo = frame.frameIndex;
    m_playheadFrame.store(frame.frameIndex);

    emit frameReady(frame);
}

void DecoderWorker::startPlayback(qint64 fromFrameIndex)
{
    if (!m_decoder->isOpen()) {
        return;
    }

    // Only reposition when the decoder is not already sitting at the right
    // place; a needless seek at the start of every play would drop the frames
    // already decoded ahead.
    if (m_decoder->nextFrameIndex() != fromFrameIndex) {
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
    }

    m_playing = true;
    m_reachedEnd = false;
    m_playheadFrame.store(fromFrameIndex);
    m_decodedAheadTo = fromFrameIndex - 1;

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
    if (m_stepScheduled || !m_playing) {
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

    if (!m_playing || !m_decoder->isOpen()) {
        return;
    }

    pumpAudio();

    const int64_t playhead = m_playheadFrame.load();
    const bool farEnoughAhead = m_decodedAheadTo >= playhead + kDecodeAheadFrames;

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
            emit endOfStream();
        } else {
            m_decodedAheadTo = frame.frameIndex;
            emit frameReady(frame);
        }
    }

    if (!m_playing) {
        return;
    }

    if (farEnoughAhead || m_reachedEnd) {
        // Nothing useful to do right now. Come back shortly rather than
        // spinning the event loop at full speed.
        m_stepScheduled = true;
        QTimer::singleShot(4, this, [this] {
            m_stepScheduled = false;
            scheduleNextStep();
        });
        return;
    }

    scheduleNextStep();
}

void DecoderWorker::pumpAudio()
{
    if (!m_audioBuffer || !m_decoder->isOpen() || !m_decoder->metadata().hasAudio) {
        return;
    }
    if (!m_decoder->outputAudioFormat().isValid()) {
        return;
    }

    // Push whatever is left of the previous chunk first, then decode more while
    // the buffer keeps accepting it. The buffer being full is the signal to
    // stop -- that is what paces audio decoding to real time.
    while (true) {
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
        if (m_pendingAudioOffset >= m_pendingAudio.pcm.size()) {
            m_pendingAudio = AudioChunk{};
            m_pendingAudioOffset = 0;
        }
    }
}

} // namespace atk::media
