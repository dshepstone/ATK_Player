#include "media/FrameCache.h"

#include <utility>

namespace atk::media {

FrameCache::FrameCache(int64_t budgetBytes)
    : m_budgetBytes(budgetBytes > 0 ? budgetBytes : kDefaultBudgetBytes)
{
}

const VideoFrame* FrameCache::find(int64_t frameIndex)
{
    const auto it = m_entries.find(frameIndex);
    if (it == m_entries.end()) {
        return nullptr;
    }
    // Promote to most recently used.
    m_lru.splice(m_lru.begin(), m_lru, it->second.lruPosition);
    it->second.lruPosition = m_lru.begin();
    return &it->second.frame;
}

bool FrameCache::contains(int64_t frameIndex) const
{
    return m_entries.find(frameIndex) != m_entries.end();
}

void FrameCache::setSourceGeneration(uint64_t generation)
{
    if (m_sourceGeneration == generation) {
        return;
    }
    // Everything held belongs to the previous source.
    clear();
    m_sourceGeneration = generation;
}

void FrameCache::insert(VideoFrame frame)
{
    // Refuse frames from a source that is no longer open. This is the backstop
    // that makes a missed invalidation somewhere else harmless rather than
    // visible as a frame from the wrong file.
    if (frame.sourceGeneration != m_sourceGeneration) {
        return;
    }

    if (!frame.isValid()) {
        return;
    }

    const int64_t bytes = frame.sizeInBytes();
    if (bytes > m_budgetBytes) {
        // A single frame that cannot fit would evict everything and then be
        // dropped anyway, so refuse it and leave the cache intact.
        return;
    }

    const int64_t index = frame.frameIndex;
    remove(index);

    m_lru.push_front(index);
    m_entries.emplace(index, Entry{ std::move(frame), m_lru.begin() });
    m_usedBytes += bytes;

    evictToBudget();
}

void FrameCache::remove(int64_t frameIndex)
{
    const auto it = m_entries.find(frameIndex);
    if (it == m_entries.end()) {
        return;
    }
    m_usedBytes -= it->second.frame.sizeInBytes();
    m_lru.erase(it->second.lruPosition);
    m_entries.erase(it);
}

void FrameCache::clear()
{
    m_entries.clear();
    m_lru.clear();
    m_usedBytes = 0;
}

void FrameCache::setBudgetBytes(int64_t bytes)
{
    if (bytes <= 0) {
        return;
    }
    m_budgetBytes = bytes;
    evictToBudget();
}

void FrameCache::evictToBudget()
{
    while (m_usedBytes > m_budgetBytes && !m_lru.empty()) {
        remove(m_lru.back());
    }
}

} // namespace atk::media
