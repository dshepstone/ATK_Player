#include "media/ScrubAudioWorker.h"

#include "core/Logging.h"
#include "media/AudioSourceReader.h"

#include <algorithm>

namespace atk::media {

ScrubAudioWorker::ScrubAudioWorker(QObject* parent)
    : QObject(parent)
    , m_reader(std::make_unique<AudioSourceReader>())
{
}

ScrubAudioWorker::~ScrubAudioWorker()
{
    m_reader.reset();
}

void ScrubAudioWorker::shutdown()
{
    m_shuttingDown = true;
    requestCancel();
    m_cache.clear();
    m_cacheOrder.clear();
    m_reader->close();
}

void ScrubAudioWorker::openSource(const QString& filePath, int sampleRate,
                                  int channelCount, quint64 sourceGeneration)
{
    if (m_shuttingDown) {
        return;
    }

    closeSource();

    m_format.sampleRate = sampleRate;
    m_format.channelCount = channelCount;
    m_format.bytesPerSample = 2;
    m_sourceGeneration = sourceGeneration;

    QString error;
    if (!m_reader->open(filePath, m_format, &error)) {
        // Media without audio is ordinary, not a failure worth showing.
        qCDebug(log::media).noquote() << "Scrub audio unavailable:" << error;
        emit sourceUnavailable(sourceGeneration);
        return;
    }

    qCInfo(log::media).noquote()
        << "Scrub audio source ready:" << sampleRate << "Hz" << channelCount << "ch";
}

void ScrubAudioWorker::closeSource()
{
    m_cache.clear();
    m_cacheOrder.clear();
    m_reader->close();
}

bool ScrubAudioWorker::cachedGrain(int64_t alignedUs, QByteArray& pcm) const
{
    const auto it = m_cache.find(alignedUs);
    if (it == m_cache.end()) {
        return false;
    }

    m_cacheOrder.remove(alignedUs);
    m_cacheOrder.push_front(alignedUs);
    pcm = it->second;
    return true;
}

void ScrubAudioWorker::cacheGrain(int64_t alignedUs, const QByteArray& pcm)
{
    if (m_cache.find(alignedUs) != m_cache.end()) {
        m_cacheOrder.remove(alignedUs);
    }
    m_cache[alignedUs] = pcm;
    m_cacheOrder.push_front(alignedUs);

    while (static_cast<int>(m_cacheOrder.size()) > kMaxCachedGrains) {
        const int64_t oldest = m_cacheOrder.back();
        m_cacheOrder.pop_back();
        m_cache.erase(oldest);
    }
}

void ScrubAudioWorker::requestGrain(qint64 mediaUs, qint64 durationUs, quint64 sequence,
                                    quint64 sourceGeneration)
{
    if (m_shuttingDown || !m_reader->isOpen()) {
        return;
    }

    // Remember the newest request even if this one is about to be dropped, so
    // older queued requests behind it are recognised as superseded too.
    quint64 newest = m_newestSequence.load(std::memory_order_acquire);
    while (sequence > newest
           && !m_newestSequence.compare_exchange_weak(newest, sequence,
                                                      std::memory_order_acq_rel)) {
        // retry with the updated value
    }

    if (sourceGeneration != m_sourceGeneration) {
        return;
    }
    if (isSuperseded(sequence)) {
        // The pointer has already moved on; decoding this would only produce
        // audio for a position the user has left.
        return;
    }

    const int64_t aligned = (std::max<int64_t>(0, mediaUs) / kGrainAlignUs) * kGrainAlignUs;

    QByteArray pcm;
    if (cachedGrain(aligned, pcm)) {
        emit grainReady(pcm, mediaUs, aligned, sequence, sourceGeneration);
        return;
    }

    int64_t actualStartUs = aligned;
    QString error;
    if (!m_reader->readRange(aligned, durationUs, pcm, &actualStartUs, &error)) {
        qCDebug(log::media).noquote()
            << "Scrub grain unavailable at" << aligned << "us:" << error;
        return;
    }

    // Checked again: decoding can take long enough for the pointer to move.
    if (isSuperseded(sequence) || sourceGeneration != m_sourceGeneration) {
        // Still worth caching -- the user is dragging through this region and
        // will very likely ask for it again a moment later.
        cacheGrain(aligned, pcm);
        return;
    }

    cacheGrain(aligned, pcm);
    emit grainReady(pcm, mediaUs, actualStartUs, sequence, sourceGeneration);
}

} // namespace atk::media
