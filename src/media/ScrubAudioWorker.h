#pragma once

#include "media/AudioBuffer.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <list>
#include <map>
#include <memory>

namespace atk::media {

class AudioSourceReader;

/// Decodes short audio grains at requested media positions, for scrubbing.
///
/// THREAD OWNERSHIP
/// ----------------
/// Its own QThread and its own AudioSourceReader -- a separate file handle and
/// separate FFmpeg contexts from both the playback decoder and the waveform
/// worker. That separation is what lets a grain be fetched without disturbing
/// the video decoder's position, which M1 deliberately made sticky so nearby
/// scrub targets decode forward instead of re-seeking.
///
/// LATEST POSITION WINS
/// --------------------
/// Requests carry a monotonically increasing sequence number. Before decoding,
/// and again before emitting, the worker checks whether a newer request has
/// arrived and abandons superseded work. Dragging quickly therefore does not
/// build a backlog of grains trailing the cursor -- the newest position is the
/// one that gets decoded and heard.
///
/// GRAIN CACHE
/// -----------
/// Grains are cached by their aligned start time. Review scrubbing goes back
/// and forth over the same second or two repeatedly, so the cache turns most
/// requests into a memory lookup instead of a seek and decode. It is bounded by
/// count, not by clip duration, so an hour-long file costs the same as a short
/// one.
class ScrubAudioWorker : public QObject {
    Q_OBJECT

public:
    explicit ScrubAudioWorker(QObject* parent = nullptr);
    ~ScrubAudioWorker() override;

    /// Grains retained. At 80 ms each and 48 kHz stereo 16-bit (about 15 kB per
    /// grain) this is roughly 4 MB and covers about 25 seconds of review
    /// material -- far more than a drag revisits, and negligible next to the
    /// video caches.
    static constexpr int kMaxCachedGrains = 256;

    /// Requests are snapped to this grid before decoding.
    ///
    /// Without alignment, every pixel of mouse movement would be a distinct
    /// cache key and nothing would ever hit. Snapping to 20 ms means a slow
    /// drag reuses grains while staying well inside a frame at 24 fps (41.7 ms),
    /// so the alignment is never audible as a timing error.
    static constexpr int64_t kGrainAlignUs = 20'000;

    void requestCancel() { m_cancelled.store(true, std::memory_order_release); }

public slots:
    /// Opens the file for grain extraction in `format`.
    void openSource(const QString& filePath, int sampleRate, int channelCount,
                    quint64 sourceGeneration);

    void closeSource();

    /// Fetches a grain *centred* on `mediaUs`.
    ///
    /// Centred rather than starting there: when the pointer sits on a frame,
    /// what a reviewer expects to hear is the sound at that frame, not the
    /// 80 ms that follow it. With a start-aligned grain every position sounds
    /// slightly late, which for lip-sync work is the wrong answer by half a
    /// grain. The audible middle now lines up with the picture.
    ///
    /// `sequence` must increase; a request older than the newest seen is
    /// dropped.
    void requestGrain(qint64 mediaUs, qint64 durationUs, quint64 sequence,
                      quint64 sourceGeneration);

    /// Releases the reader on the owning thread, before the thread is joined.
    void shutdown();

signals:
    /// A decoded grain. `actualStartUs` is where the audio really begins, which
    /// is what the accuracy tests assert against the requested position.
    void grainReady(const QByteArray& pcm, qint64 requestedUs, qint64 actualStartUs,
                    quint64 sequence, quint64 sourceGeneration);

    /// The source has no usable audio; scrubbing continues visually.
    void sourceUnavailable(quint64 sourceGeneration);

private:
    /// Newest sequence requested. Set from the UI thread, read here.
    std::atomic<quint64> m_newestSequence{ 0 };
    std::atomic<bool> m_cancelled{ false };

    bool isSuperseded(quint64 sequence) const
    {
        return sequence < m_newestSequence.load(std::memory_order_acquire);
    }

    /// Looks up a cached grain, promoting it to most recently used.
    bool cachedGrain(int64_t alignedUs, QByteArray& pcm) const;
    void cacheGrain(int64_t alignedUs, const QByteArray& pcm);

    std::unique_ptr<AudioSourceReader> m_reader;
    AudioFormat m_format;
    quint64 m_sourceGeneration = 0;
    bool m_shuttingDown = false;

    /// Aligned start time -> PCM, with an LRU order alongside.
    mutable std::map<int64_t, QByteArray> m_cache;
    mutable std::list<int64_t> m_cacheOrder;
};

} // namespace atk::media
