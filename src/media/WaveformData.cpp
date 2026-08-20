#include "media/WaveformData.h"

#include <algorithm>

namespace atk::media {
namespace {

WaveformPeak combine(const WaveformPeak& a, const WaveformPeak& b)
{
    WaveformPeak merged;
    merged.minimum = std::min(a.minimum, b.minimum);
    merged.maximum = std::max(a.maximum, b.maximum);
    return merged;
}

} // namespace

void WaveformData::setSourceGeneration(uint64_t generation)
{
    if (m_sourceGeneration == generation) {
        return;
    }
    clear();
    m_sourceGeneration = generation;
}

void WaveformData::clear()
{
    m_levels.clear();
    m_complete = false;
}

int64_t WaveformData::bucketDurationUs(int level)
{
    return kBaseBucketUs * (int64_t(1) << std::clamp(level, 0, kLevelCount - 1));
}

int64_t WaveformData::coveredUs() const
{
    if (m_levels.isEmpty()) {
        return 0;
    }
    return static_cast<int64_t>(m_levels.at(0).size()) * kBaseBucketUs;
}

void WaveformData::appendBaseBuckets(const QVector<WaveformPeak>& buckets)
{
    if (buckets.isEmpty()) {
        return;
    }

    if (m_levels.isEmpty()) {
        m_levels.resize(kLevelCount);
    }

    m_levels[0].append(buckets);

    // Fold upward. Each level is rebuilt only from the tail that its parent has
    // newly completed, so appending stays proportional to what arrived rather
    // than to the whole file -- which matters because analysis delivers chunks
    // continuously while the user is already interacting with the timeline.
    for (int level = 1; level < kLevelCount; ++level) {
        const QVector<WaveformPeak>& finer = m_levels.at(level - 1);
        QVector<WaveformPeak>& coarser = m_levels[level];

        // Only whole pairs can be folded; a trailing odd bucket waits for its
        // partner rather than being emitted at half coverage and then being
        // wrong once the next chunk arrives.
        const int completePairs = finer.size() / 2;
        if (completePairs <= coarser.size()) {
            break;
        }

        coarser.reserve(completePairs);
        for (int index = coarser.size(); index < completePairs; ++index) {
            coarser.append(combine(finer.at(index * 2), finer.at(index * 2 + 1)));
        }
    }
}

int WaveformData::levelForBucketDuration(int64_t targetUs) const
{
    if (targetUs <= kBaseBucketUs) {
        return 0;
    }
    for (int level = kLevelCount - 1; level > 0; --level) {
        if (bucketDurationUs(level) <= targetUs) {
            return level;
        }
    }
    return 0;
}

const QVector<WaveformPeak>& WaveformData::level(int index) const
{
    static const QVector<WaveformPeak> empty;
    if (index < 0 || index >= m_levels.size()) {
        return empty;
    }
    return m_levels.at(index);
}

WaveformPeak WaveformData::peakAt(int level, int64_t mediaUs) const
{
    const QVector<WaveformPeak>& buckets = this->level(level);
    if (buckets.isEmpty() || mediaUs < 0) {
        return {};
    }

    const int64_t index = mediaUs / bucketDurationUs(level);
    if (index < 0 || index >= buckets.size()) {
        return {};
    }
    return buckets.at(static_cast<int>(index));
}

WaveformPeak WaveformData::peakOverRange(int level, int64_t startUs, int64_t endUs) const
{
    const QVector<WaveformPeak>& buckets = this->level(level);
    if (buckets.isEmpty() || endUs <= startUs) {
        return {};
    }

    const int64_t duration = bucketDurationUs(level);
    const int64_t firstIndex = std::max<int64_t>(0, startUs / duration);
    // Inclusive of the bucket containing the last microsecond of the range.
    const int64_t lastIndex = std::min<int64_t>(buckets.size() - 1, (endUs - 1) / duration);

    if (firstIndex > lastIndex) {
        return {};
    }

    WaveformPeak merged = buckets.at(static_cast<int>(firstIndex));
    for (int64_t index = firstIndex + 1; index <= lastIndex; ++index) {
        merged = combine(merged, buckets.at(static_cast<int>(index)));
    }
    return merged;
}

int64_t WaveformData::memoryBytes() const
{
    int64_t total = 0;
    for (const QVector<WaveformPeak>& buckets : m_levels) {
        total += static_cast<int64_t>(buckets.capacity()) * sizeof(WaveformPeak);
    }
    return total;
}

} // namespace atk::media
