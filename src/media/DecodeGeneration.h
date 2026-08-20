#pragma once

#include <atomic>
#include <cstdint>

namespace atk::media {

/// Generation counters that let stale decoder results be recognised and thrown
/// away instead of being displayed.
///
/// WHY THIS EXISTS
/// ---------------
/// Decoding is asynchronous, and a decode already in progress cannot be
/// un-started. Drag the playhead and the worker may be halfway through decoding
/// frame 500 when the user arrives at frame 900. Without a way to tell that the
/// frame-500 result is obsolete, it arrives late and is presented -- the picture
/// visibly jumps backwards after the seek has finished. The same hazard is worse
/// across a media change: a frame from the previous file can appear under the
/// new file's name.
///
/// Ordering the requests is not sufficient on its own. A queued request is only
/// superseded while it is still queued; once the worker has picked it up, the
/// result is coming regardless. So results carry the generation they were
/// requested under, and anything that no longer matches is dropped.
///
/// TWO COUNTERS, NOT ONE
/// ---------------------
/// `request` changes on every seek, step and playback start. `source` changes
/// only when the media itself changes. They answer different questions:
///
///   request -- "is this frame still the one being waited for?"
///   source  -- "does this frame even belong to the file that is open?"
///
/// The cache keys on `source`, because frames from one file stay valid across
/// any number of seeks within it, and only become garbage when the file changes.
/// Collapsing both into one counter would throw the cache away on every seek and
/// destroy the point of having one.
///
/// THREADING
/// ---------
/// Bumped only by the controller on the UI thread; read by the decode thread.
/// Atomics rather than a mutex because the read is on the decoder's inner loop
/// and must be close to free.
class DecodeGenerations {
public:
    /// Invalidates in-flight requests. Called for every seek, step and playback
    /// start. Returns the new request generation to send with the request.
    uint64_t bumpRequest() noexcept
    {
        return m_request.fetch_add(1, std::memory_order_release) + 1;
    }

    /// Invalidates everything: in-flight requests *and* every cached frame.
    /// Called when the open media changes. Returns the new source generation.
    uint64_t bumpSource() noexcept
    {
        // A new source necessarily invalidates outstanding requests too, so the
        // request counter moves with it. Forgetting this is how a frame from
        // the previous file slips through.
        m_request.fetch_add(1, std::memory_order_release);
        return m_source.fetch_add(1, std::memory_order_release) + 1;
    }

    uint64_t currentRequest() const noexcept
    {
        return m_request.load(std::memory_order_acquire);
    }

    uint64_t currentSource() const noexcept
    {
        return m_source.load(std::memory_order_acquire);
    }

    /// True when a result tagged `generation` is still wanted.
    bool isCurrentRequest(uint64_t generation) const noexcept
    {
        return generation == currentRequest();
    }

    /// True when a frame produced under `generation` belongs to the open media.
    bool isCurrentSource(uint64_t generation) const noexcept
    {
        return generation == currentSource();
    }

private:
    std::atomic<uint64_t> m_request{ 0 };
    std::atomic<uint64_t> m_source{ 0 };
};

} // namespace atk::media
