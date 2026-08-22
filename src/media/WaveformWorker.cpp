#include "media/WaveformWorker.h"

#include "core/Logging.h"
#include "media/AudioSourceReader.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

namespace atk::media {
namespace {

/// Reads interleaved signed 16-bit PCM as a normalised float.
inline float sampleToFloat(const char* data, qsizetype offset)
{
    const auto raw = static_cast<int16_t>(
        static_cast<uint8_t>(data[offset]) | (static_cast<uint8_t>(data[offset + 1]) << 8));
    return static_cast<float>(raw) / 32768.0f;
}

} // namespace

WaveformWorker::WaveformWorker(QObject* parent)
    : QObject(parent)
    , m_reader(std::make_unique<AudioSourceReader>())
{
}

WaveformWorker::~WaveformWorker()
{
    m_reader.reset();
}

void WaveformWorker::shutdown()
{
    m_shuttingDown = true;
    requestCancel();
    m_reader->close();
}

void WaveformWorker::analyse(const QString& filePath, quint64 sourceGeneration)
{
    if (m_shuttingDown
        || sourceGeneration != m_latestGeneration.load(std::memory_order_acquire)) {
        return;
    }
    m_cancelled.store(false, std::memory_order_release);

    m_reader->close();

    // Mono at a reduced rate: the M2 waveform is a single merged envelope, so
    // decoding every channel at full rate would cost more and be discarded.
    // Merging rather than stacking channels is the simpler first
    // representation, and for dialogue review -- where the voice is usually
    // centred -- a merged envelope shows the same syllable structure.
    AudioFormat analysisFormat;
    analysisFormat.sampleRate = kAnalysisSampleRate;
    analysisFormat.channelCount = 1;
    analysisFormat.bytesPerSample = 2;

    QString error;
    if (!m_reader->open(filePath, analysisFormat, &error)) {
        qCDebug(log::media).noquote() << "Waveform analysis unavailable:" << error;
        emit analysisUnavailable(error, sourceGeneration);
        return;
    }

    QElapsedTimer timer;
    timer.start();

    const int64_t bucketUs = WaveformData::kBaseBucketUs;
    const int64_t samplesPerBucket =
        std::max<int64_t>(1, (int64_t(analysisFormat.sampleRate) * bucketUs) / 1'000'000);

    QVector<WaveformPeak> pending;
    pending.reserve(kChunkBuckets);

    // Bucket accumulation carries across chunk boundaries: a decoded chunk
    // rarely ends exactly on a bucket edge, and resetting at each boundary
    // would put a spurious notch in the waveform every few hundred
    // milliseconds.
    WaveformPeak current;
    int64_t samplesInBucket = 0;
    bool bucketStarted = false;
    int64_t totalBuckets = 0;

    const bool completed = m_reader->scan(
        [&](const AudioChunk& chunk) {
            const char* data = chunk.pcm.constData();
            const qsizetype sampleCount = chunk.pcm.size() / 2;

            for (qsizetype index = 0; index < sampleCount; ++index) {
                const float value = sampleToFloat(data, index * 2);

                if (!bucketStarted) {
                    current.minimum = value;
                    current.maximum = value;
                    bucketStarted = true;
                } else {
                    current.minimum = std::min(current.minimum, value);
                    current.maximum = std::max(current.maximum, value);
                }

                if (++samplesInBucket >= samplesPerBucket) {
                    pending.append(current);
                    ++totalBuckets;
                    samplesInBucket = 0;
                    bucketStarted = false;

                    if (pending.size() >= kChunkBuckets) {
                        emit peaksReady(pending, sourceGeneration);
                        pending.clear();
                        pending.reserve(kChunkBuckets);
                    }
                }
            }
        },
        [this, sourceGeneration] {
            return isCancelled()
                || sourceGeneration != m_latestGeneration.load(std::memory_order_acquire);
        },
        &error);

    if (isCancelled() || m_shuttingDown) {
        qCDebug(log::media) << "Waveform analysis cancelled after" << totalBuckets << "buckets";
        m_reader->close();
        return;
    }

    // Whatever is left of a partial bucket still represents real audio.
    if (bucketStarted) {
        pending.append(current);
        ++totalBuckets;
    }
    if (!pending.isEmpty()) {
        emit peaksReady(pending, sourceGeneration);
    }

    const int64_t coveredUs = totalBuckets * bucketUs;
    qCInfo(log::media).noquote()
        << QStringLiteral("Waveform analysed: %1 buckets covering %2 s in %3 ms%4")
               .arg(totalBuckets)
               .arg(coveredUs / 1'000'000.0, 0, 'f', 2)
               .arg(timer.elapsed())
               .arg(completed ? QString() : QStringLiteral(" (incomplete)"));

    emit analysisFinished(coveredUs, sourceGeneration);
    m_reader->close();
}

} // namespace atk::media
