#pragma once

#include "media/WaveformData.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

namespace atk::media {

class AudioSourceReader;

/// Scans a file's audio in the background and emits waveform peaks as they are
/// produced.
///
/// THREAD OWNERSHIP
/// ----------------
/// Moved to its own QThread and owns an AudioSourceReader -- a completely
/// separate set of FFmpeg contexts from the playback decoder, on a separate
/// file handle. Nothing is shared, so a full-file scan cannot move the
/// playback decoder's position or compete with it for a mutex.
///
/// PROGRESSIVE DELIVERY
/// --------------------
/// Peaks are emitted in chunks rather than once at the end. Scanning an hour of
/// audio takes seconds even at decode speed, and a timeline that stays blank
/// until it finishes looks broken. Emitting as it goes means the waveform fills
/// in left to right while the user is already working.
///
/// CANCELLATION
/// ------------
/// The scan checks a cancellation flag between chunks. Opening another file or
/// closing the application sets it, so the worker abandons its scan promptly
/// instead of holding shutdown open for the length of a file.
class WaveformWorker : public QObject {
    Q_OBJECT

public:
    explicit WaveformWorker(QObject* parent = nullptr);
    ~WaveformWorker() override;

    /// Peaks emitted per signal. Small enough that the timeline updates
    /// visibly as analysis proceeds, large enough not to flood the event loop.
    static constexpr int kChunkBuckets = 256;

    /// Format the analysis decodes to. Mono, because the M2 waveform is a
    /// merged envelope -- see analyse(). 22.05 kHz is ample for an amplitude
    /// envelope bucketed at 10 ms and halves the resampling work.
    static constexpr int kAnalysisSampleRate = 22050;

    /// Set from the UI thread to abandon an in-flight scan.
    void requestCancel() { m_cancelled.store(true, std::memory_order_release); }
    void requestAnalysis(quint64 generation)
    {
        m_latestGeneration.store(generation, std::memory_order_release);
        requestCancel();
    }

public slots:
    /// Scans `filePath`, emitting peaks tagged with `sourceGeneration`.
    void analyse(const QString& filePath, quint64 sourceGeneration);

    /// Releases the reader on the owning thread, before the thread is joined.
    void shutdown();

signals:
    /// A run of level-0 buckets, continuing from whatever was emitted before.
    void peaksReady(const QVector<atk::media::WaveformPeak>& peaks, quint64 sourceGeneration);

    /// The scan finished; `totalUs` is the audio duration actually covered.
    void analysisFinished(qint64 totalUs, quint64 sourceGeneration);

    /// The file had no analysable audio, or decoding failed. Not an error the
    /// user needs to see -- the timeline simply shows no waveform.
    void analysisUnavailable(const QString& reason, quint64 sourceGeneration);

private:
    bool isCancelled() const { return m_cancelled.load(std::memory_order_acquire); }

    std::unique_ptr<AudioSourceReader> m_reader;
    std::atomic<bool> m_cancelled{ false };
    std::atomic<quint64> m_latestGeneration{ 0 };
    bool m_shuttingDown = false;
};

} // namespace atk::media

Q_DECLARE_METATYPE(QVector<atk::media::WaveformPeak>)
