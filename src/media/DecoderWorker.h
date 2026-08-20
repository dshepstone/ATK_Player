#pragma once

#include "media/AudioBuffer.h"
#include "media/MediaDecoder.h"
#include "media/MediaMetadata.h"
#include "media/VideoFrame.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

namespace atk::audio { class AudioRingBuffer; }

namespace atk::media {

/// Runs the FFmpeg decoder on its own thread.
///
/// THREAD OWNERSHIP
/// ----------------
/// This object is moved to a dedicated QThread and every one of its slots runs
/// there. The MediaDecoder it owns -- and therefore every FFmpeg context -- is
/// touched only from that thread. The UI thread never calls into it directly;
/// it emits requests, which arrive as queued slot invocations.
///
/// That is what keeps the interface responsive: a seek into the middle of a
/// long GOP can take tens of milliseconds of decoding, and doing that on the UI
/// thread would freeze the window every time the user dragged the playhead.
///
/// HOW PLAYBACK IS DRIVEN
/// ----------------------
/// decodeStep() decodes at most one video frame and then re-posts itself as a
/// queued call rather than looping. Anything the UI has since requested -- a
/// seek, a pause, a close -- is already sitting in the same event queue and is
/// therefore processed before the next step. Obsolete decoding is superseded
/// within one frame's work, with no cancellation flags to get wrong.
class DecoderWorker : public QObject {
    Q_OBJECT

public:
    /// `audioBuffer` is shared with the audio output and may be null for
    /// video-only use (the tests do this).
    explicit DecoderWorker(std::shared_ptr<audio::AudioRingBuffer> audioBuffer,
                           QObject* parent = nullptr);
    ~DecoderWorker() override;

    /// How far ahead of the playhead the worker will decode during playback.
    /// Enough to absorb a slow frame, small enough that a seek does not throw
    /// away much work.
    static constexpr int kDecodeAheadFrames = 24;

public slots:
    void openMedia(const QString& filePath);
    void closeMedia();

    /// Decodes and emits exactly this frame. Used by stepping and seeking.
    void requestFrame(qint64 frameIndex);

    /// Begins continuous decoding from `fromFrameIndex`.
    void startPlayback(qint64 fromFrameIndex);
    void stopPlayback();

    /// Tells the worker where the playhead is, so it knows how far to decode
    /// ahead and when to stop filling the queue.
    void setPlayheadFrame(qint64 frameIndex);

    /// Configures audio resampling to the device's format.
    void configureAudio(int sampleRate, int channelCount);

signals:
    void mediaOpened(const atk::media::MediaMetadata& metadata);
    void mediaOpenFailed(const QString& message);
    void mediaClosed();

    /// A decoded, display-ready frame. Delivered for both stepping and playback.
    void frameReady(const atk::media::VideoFrame& frame);

    /// The requested frame could not be produced.
    void frameFailed(qint64 frameIndex, const QString& message);

    /// Decoding reached the end of the video stream.
    void endOfStream();

    /// A non-fatal decode problem worth surfacing.
    void decodeError(const QString& message);

private slots:
    /// One unit of decoding. Re-posts itself while playback is active.
    void decodeStep();

private:
    void pumpAudio();
    void scheduleNextStep();

    std::unique_ptr<MediaDecoder> m_decoder;
    std::shared_ptr<audio::AudioRingBuffer> m_audioBuffer;

    /// Audio decoded but not yet accepted by the ring buffer, held so no
    /// samples are lost when the buffer is momentarily full.
    AudioChunk m_pendingAudio;
    int64_t m_pendingAudioOffset = 0;

    bool m_playing = false;
    bool m_stepScheduled = false;
    bool m_reachedEnd = false;

    /// Last playhead position reported by the controller.
    std::atomic<int64_t> m_playheadFrame{ 0 };
    /// Highest frame index handed to the controller during this playback run.
    int64_t m_decodedAheadTo = -1;
};

} // namespace atk::media

Q_DECLARE_METATYPE(atk::media::MediaMetadata)
Q_DECLARE_METATYPE(atk::media::VideoFrame)
