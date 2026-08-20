#include "media/FrameCache.h"

#include <utility>

namespace atk::media {

FrameCache::FrameCache(int64_t budgetBytes)
    : m_budgetBytes(budgetBytes > 0 ? budgetBytes : kDefaultBudgetBytes)
{
}

const VideoFrame* FrameCache::find(int64_t frameNumber)
{
    const auto it = m_entries.find(frameNumber);
    if (it == m_entries.end()) {
        return nullptr;
    }
    // Promote to most recently used.
    m_lru.splice(m_lru.begin(), m_lru, it->second.lruPosition);
    it->second.lruPosition = m_lru.begin();
    return &it->second.frame;
}

bool FrameCache::contains(int64_t frameNumber) const
{
    return m_entries.find(frameNumber) != m_entries.end();
}

void FrameCache::insert(VideoFrame frame)
{
    if (!frame.isValid()) {
        return;
    }

    const int64_t bytes = frame.sizeInBytes();
    if (bytes > m_budgetBytes) {
        // A single frame that cannot fit would evict everything and then be
        // dropped anyway, so refuse it and leave the cache intact.
        return;
    }

    const int64_t number = frame.frameNumber;
    remove(number);

    m_lru.push_front(number);
    m_entries.emplace(number, Entry{ std::move(frame), m_lru.begin() });
    m_usedBytes += bytes;

    evictToBudget();
}

void FrameCache::remove(int64_t frameNumber)
{
    const auto it = m_entries.find(frameNumber);
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
