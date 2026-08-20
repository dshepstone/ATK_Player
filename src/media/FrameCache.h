#pragma once

#include "media/VideoFrame.h"

#include <cstdint>
#include <list>
#include <unordered_map>

namespace atk::media {

/// Least-recently-used cache of decoded frames, bounded by total bytes.
///
/// Animation review scrubs back and forth over a short range, so the frames
/// worth keeping are the ones touched most recently rather than the ones
/// nearest the playhead. An LRU policy with a memory budget gives that without
/// needing to know the playback direction.
///
/// Not thread-safe. From M1 the decode thread and the UI thread each hold their
/// own reference through a mutex owned by PlaybackController.
class FrameCache {
public:
    /// Default budget: 512 MiB, roughly 60 frames of 1080p RGBA.
    static constexpr int64_t kDefaultBudgetBytes = 512LL * 1024 * 1024;

    explicit FrameCache(int64_t budgetBytes = kDefaultBudgetBytes);

    FrameCache(const FrameCache&) = delete;
    FrameCache& operator=(const FrameCache&) = delete;

    /// Looks up a frame and marks it most recently used.
    /// Returns nullptr on a miss. The pointer is invalidated by the next
    /// insert() or clear().
    const VideoFrame* find(int64_t frameNumber);

    /// True without changing LRU order. For diagnostics and tests.
    bool contains(int64_t frameNumber) const;

    /// Inserts or replaces a frame, evicting least-recently-used entries until
    /// the budget is met. A frame larger than the whole budget is not cached.
    void insert(VideoFrame frame);

    void remove(int64_t frameNumber);
    void clear();

    int64_t budgetBytes() const { return m_budgetBytes; }
    void setBudgetBytes(int64_t bytes);

    int64_t usedBytes() const { return m_usedBytes; }
    std::size_t count() const { return m_entries.size(); }

private:
    void evictToBudget();

    struct Entry {
        VideoFrame frame;
        std::list<int64_t>::iterator lruPosition;
    };

    int64_t m_budgetBytes;
    int64_t m_usedBytes = 0;
    /// Front is most recently used.
    std::list<int64_t> m_lru;
    std::unordered_map<int64_t, Entry> m_entries;
};

} // namespace atk::media
