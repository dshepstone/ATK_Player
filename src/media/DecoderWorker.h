#pragma once

#include "media/AudioBuffer.h"
#include "media/DecodeGeneration.h"
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
/// there. The MediaDecoder it owns -- and therefore every FFmpeg context
/// (AVFormatContext, both AVCodecContexts, SwsContext, SwrContext, and all
/// packet and frame allocation) -- is touched only from that thread. The UI
/// thread never calls into it directly; it emits requests, which arrive as
/// queued slot invocations, and receives finished frames back the same way.
///
/// That boundary is what keeps the interface responsive: a seek into the middle
/// of a long GOP can take tens of milliseconds of decoding, and doing that on
/// the UI thread would freeze the window every time the playhead was dragged.
///
/// GENERATIONS
/// -----------
/// Every request carries the generation it was issued under. Before a result is
/// emitted the generation is checked, and anything superseded is dropped rather
/// than sent. Long decode loops also poll the generation and abandon work early.
/// Ordering alone cannot provide this: once the worker has started decoding a
/// request, the result is coming whether or not it is still wanted. See
/// DecodeGeneration.h.
///
/// HOW PLAYBACK IS DRIVEN
/// ----------------------
/// decodeStep() decodes at most one video frame and then re-posts itself as a
/// queued call rather than looping. Anything the UI has since requested -- a
/// seek, a pause, a close -- is already sitting in the same event queue and is
/// therefore processed between steps.
class DecoderWorker : public QObject {
    Q_OBJECT

public:
    /// `audioBuffer` is shared with the audio output and may be null for
    /// video-only use (the tests do this). `generations` is shared with the
    /// controller and must not be null.
    DecoderWorker(std::shared_ptr<audio::AudioRingBuffer> audioBuffer,
                  std::shared_ptr<DecodeGenerations> generations,
                  QObject* parent = nullptr);
    ~DecoderWorker() override;

    /// Fallback lookahead until the controller supplies one sized for the
    /// media. A fixed frame count cannot be right for every resolution: 24
    /// frames is 190 MB at 1080p and 796 MB at 4K.
    static constexpr int kDefaultDecodeAheadFrames = 8;

    /// Audio is kept topped up to a target rather than "as full as possible".
    ///
    /// The original pumpAudio() looped until the ring buffer was full, which on
    /// a 2-second buffer meant decoding two seconds of audio -- and demuxing
    /// all the interleaved video packets that come with it -- before returning
    /// to video work. That is a long time for the decode thread to be
    /// unavailable, and it is what made video arrive in bursts.
    static constexpr int64_t kAudioTargetMs = 400;
    static constexpr int64_t kAudioLowWatermarkMs = 200;
    static constexpr int64_t kAudioMaxMs = 750;

    /// Cap on audio pushed per decode step, so one call cannot monopolise the
    /// thread even when the buffer is far below target.
    static constexpr int64_t kMaxAudioBytesPerStep = 64 * 1024;

public slots:
    void openMedia(const QString& filePath, quint64 sourceGeneration);
    void closeMedia();

    /// Decodes and emits exactly this frame. Used by stepping and seeking.
    void requestFrame(qint64 frameIndex, quint64 requestGeneration);

    /// Begins continuous decoding from `fromFrameIndex`.
    void startPlayback(qint64 fromFrameIndex, quint64 requestGeneration);
    void stopPlayback();

    /// Tells the worker where the playhead is, so it knows how far to decode
    /// ahead and when to stop filling the queue.
    void setPlayheadFrame(qint64 frameIndex);

    /// Configures audio resampling to the device's format.
    void configureAudio(int sampleRate, int channelCount);

    /// Sets how many frames ahead of the playhead to decode. Supplied by the
    /// controller from the frame rate and frame size.
    void setLookaheadFrames(int frames);

    /// Decodes audio up to the startup target and reports how much was queued,
    /// so the controller can start the device against primed audio rather than
    /// against silence.
    void primeAudio(int targetMs);

    /// Stops all work and releases the decoder. Called during shutdown before
    /// the thread is joined, so FFmpeg teardown happens on the owning thread.
    void shutdown();

signals:
    void mediaOpened(const atk::media::MediaMetadata& metadata, quint64 sourceGeneration);
    void mediaOpenFailed(const QString& message, quint64 sourceGeneration);
    void mediaClosed();

    /// A decoded, display-ready frame, tagged with the request it satisfies.
    /// The frame itself carries its source generation.
    void frameReady(const atk::media::VideoFrame& frame, quint64 requestGeneration);

    /// The requested frame could not be produced.
    void frameFailed(qint64 frameIndex, const QString& message, quint64 requestGeneration);

    /// Decoding reached the end of the video stream.
    void endOfStream(quint64 requestGeneration);

    /// A non-fatal decode problem worth surfacing.
    void decodeError(const QString& message);

    /// Emitted after primeAudio() with the milliseconds actually queued.
    void audioPrimed(int bufferedMs, qint64 mediaOriginUs, quint64 requestGeneration);

private slots:
    /// One unit of decoding. Re-posts itself while playback is active.
    void decodeStep();

private:
    void pumpAudio();

    /// Tops the ring buffer up to `targetMs`, writing at most
    /// `maxBytesThisCall` bytes so one call cannot monopolise the thread.
    void pumpAudioUpTo(int64_t targetMs, int64_t maxBytesThisCall);

    /// Milliseconds of audio currently queued for the device.
    int64_t bufferedAudioMs() const;
    bool preparePendingAudioForEpoch();
    void scheduleNextStep();

    /// True when `generation` is no longer the current request.
    bool isStale(quint64 generation) const;

    std::unique_ptr<MediaDecoder> m_decoder;
    std::shared_ptr<audio::AudioRingBuffer> m_audioBuffer;
    std::shared_ptr<DecodeGenerations> m_generations;

    /// Audio decoded but not yet accepted by the ring buffer, held so no
    /// samples are lost when the buffer is momentarily full.
    AudioChunk m_pendingAudio;
    int64_t m_pendingAudioOffset = 0;
    int64_t m_audioTrimBeforeUs = -1;
    int64_t m_positionedFrameIndex = -1;
    int64_t m_positionedFramePtsUs = -1;

    bool m_playing = false;
    bool m_stepScheduled = false;
    bool m_reachedEnd = false;
    bool m_shuttingDown = false;

    /// Generation the current playback run was started under.
    quint64 m_playbackGeneration = 0;

    /// Last playhead position reported by the controller.
    std::atomic<int64_t> m_playheadFrame{ 0 };
    /// Highest frame index handed to the controller during this playback run.
    int64_t m_decodedAheadTo = -1;
    int m_lookaheadFrames = kDefaultDecodeAheadFrames;
};

} // namespace atk::media

Q_DECLARE_METATYPE(atk::media::MediaMetadata)
Q_DECLARE_METATYPE(atk::media::VideoFrame)
