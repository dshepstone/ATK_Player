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
    /// Default budget: 256 MiB.
    ///
    /// That is roughly 30 frames of 1080p RGB32 -- more than a second either
    /// side of the playhead at 24 fps, which is the range animation review
    /// actually scrubs over. Large enough that repeated stepping is instant,
    /// small enough that opening a 4K clip does not consume the machine.
    static constexpr int64_t kDefaultBudgetBytes = 256LL * 1024 * 1024;

    explicit FrameCache(int64_t budgetBytes = kDefaultBudgetBytes);

    FrameCache(const FrameCache&) = delete;
    FrameCache& operator=(const FrameCache&) = delete;

    // --- Source identity --------------------------------------------------
    //
    // The cache holds frames for exactly one source generation. Binding it to a
    // generation rather than relying on callers to remember to clear() is what
    // guarantees a frame from the previous file can never be shown under the
    // new one: a mismatched insert is refused and a mismatched lookup misses,
    // even if some path forgot to invalidate.

    /// Declares which source the cache now holds frames for. Changing it
    /// discards everything, because those frames belong to a file that is no
    /// longer open.
    void setSourceGeneration(uint64_t generation);
    uint64_t sourceGeneration() const { return m_sourceGeneration; }

    /// Looks up a frame and marks it most recently used.
    /// Returns nullptr on a miss. The pointer is invalidated by the next
    /// insert() or clear().
    const VideoFrame* find(int64_t frameIndex);

    /// True without changing LRU order. For diagnostics and tests.
    bool contains(int64_t frameIndex) const;

    /// Inserts or replaces a frame, evicting least-recently-used entries until
    /// the budget is met. A frame larger than the whole budget is not cached.
    ///
    /// A frame whose sourceGeneration does not match the cache's is ignored --
    /// that is a late result from a file that is no longer open.
    void insert(VideoFrame frame);

    void remove(int64_t frameIndex);
    void clear();

    int64_t budgetBytes() const { return m_budgetBytes; }
    void setBudgetBytes(int64_t bytes);

    int64_t usedBytes() const { return m_usedBytes; }

    // --- Diagnostics ------------------------------------------------------
    // Cheap counters, not conditionally compiled: the cost is an increment and
    // they are the only way to tell a cache that is working from one that is
    // evicting its own lookahead before it can be presented.
    int64_t hitCount() const { return m_hits; }
    int64_t missCount() const { return m_misses; }
    int64_t evictionCount() const { return m_evictions; }
    void resetCounters() { m_hits = 0; m_misses = 0; m_evictions = 0; }
    std::size_t count() const { return m_entries.size(); }

private:
    void evictToBudget();

    struct Entry {
        VideoFrame frame;
        std::list<int64_t>::iterator lruPosition;
    };

    int64_t m_budgetBytes;
    int64_t m_usedBytes = 0;
    uint64_t m_sourceGeneration = 0;
    int64_t m_hits = 0;
    int64_t m_misses = 0;
    int64_t m_evictions = 0;
    /// Front is most recently used.
    std::list<int64_t> m_lru;
    std::unordered_map<int64_t, Entry> m_entries;
};

} // namespace atk::media
