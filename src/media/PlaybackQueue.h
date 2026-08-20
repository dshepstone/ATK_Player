#pragma once

#include "media/VideoFrame.h"

#include <cstdint>
#include <map>

namespace atk::media {

/// Ordered, bounded buffer of frames waiting to be presented.
///
/// WHY THIS IS NOT THE FrameCache
/// ------------------------------
/// Playback originally read straight out of the LRU FrameCache, and that does
/// not work. A 1080p frame in RGB32 is 8.29 MB, so a 256 MiB budget holds about
/// thirty of them; with a lookahead of twenty-four plus the frames already
/// shown, the cache is permanently over budget and evicting. What it evicts is
/// whatever was touched least recently -- which includes frames decoded ahead
/// that the playhead has not reached yet. The decoder produces a frame, the
/// cache throws it away, and the display tick then misses and holds the old
/// picture. That is exactly the "buffer cannot stay ahead" stutter. At 4K
/// (33 MB per frame) a twenty-four frame lookahead wants 796 MB and the cache
/// can hold seven, so it thrashes continuously.
///
/// The two structures want opposite policies:
///
///   FrameCache     -- keep what was used *recently*, for stepping and scrubbing
///                     back over the same few seconds. LRU is right.
///   PlaybackQueue  -- keep what is about to be needed, in order, and never
///                     discard it early. FIFO by frame index is right.
///
/// So playback gets its own queue. Frames still go into the cache as well, so
/// stepping backwards over just-played material stays instant, but eviction
/// there can no longer starve playback.
///
/// Not thread-safe; owned by PlaybackController on the UI thread.
class PlaybackQueue {
public:
    /// Frames whose index is below the presented one are dropped, so the queue
    /// only ever holds the future. Returns how many were discarded.
    int discardUpTo(int64_t frameIndex);

    /// Adds a frame. Frames from a different source generation are refused, for
    /// the same reason FrameCache refuses them.
    void insert(VideoFrame frame);

    /// The frame for `frameIndex`, or nullptr. Does not remove it -- the
    /// display tick calls discardUpTo() once it has actually presented.
    const VideoFrame* find(int64_t frameIndex) const;

    /// Lowest and highest indices held. -1 when empty.
    int64_t firstFrame() const;
    int64_t lastFrame() const;

    void clear();
    void setSourceGeneration(uint64_t generation);
    uint64_t sourceGeneration() const { return m_sourceGeneration; }

    std::size_t count() const { return m_frames.size(); }
    int64_t usedBytes() const { return m_usedBytes; }
    bool isEmpty() const { return m_frames.empty(); }

    /// Cap on retained bytes. Reaching it means the decoder is far enough ahead
    /// and should pause rather than keep allocating.
    void setBudgetBytes(int64_t bytes) { m_budgetBytes = bytes; }
    int64_t budgetBytes() const { return m_budgetBytes; }
    bool isFull() const { return m_usedBytes >= m_budgetBytes; }

    /// Default queue budget: 96 MiB.
    ///
    /// Sized to hold a useful lookahead at 1080p (about eleven frames) without
    /// competing with the review cache for the same memory. The lookahead
    /// *target* is computed from time and frame size separately; this is the
    /// hard ceiling that keeps 4K from running away.
    static constexpr int64_t kDefaultBudgetBytes = 96LL * 1024 * 1024;

private:
    /// std::map, so iteration is in presentation order and the oldest frame is
    /// always begin(). That ordering is the whole point of the structure.
    std::map<int64_t, VideoFrame> m_frames;
    int64_t m_usedBytes = 0;
    int64_t m_budgetBytes = kDefaultBudgetBytes;
    uint64_t m_sourceGeneration = 0;
};

} // namespace atk::media
