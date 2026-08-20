#pragma once

#include <cstdint>

namespace atk::timeline {

/// An in/out range over the timeline.
///
/// Set with the I and O commands. When disabled the whole source plays; when
/// enabled, playback and looping are confined to [start, end] inclusive.
struct PlaybackRange {
    int64_t startFrame = 0;
    int64_t endFrame = 0;
    bool enabled = false;

    constexpr bool isValid() const { return endFrame >= startFrame && startFrame >= 0; }

    /// Inclusive length in frames. Zero when invalid.
    constexpr int64_t frameCount() const
    {
        return isValid() ? (endFrame - startFrame + 1) : 0;
    }

    constexpr bool contains(int64_t frame) const
    {
        return isValid() && frame >= startFrame && frame <= endFrame;
    }

    /// Returns `frame` moved inside the range. Unclamped when the range is
    /// disabled or invalid.
    constexpr int64_t clamp(int64_t frame) const
    {
        if (!enabled || !isValid()) {
            return frame;
        }
        if (frame < startFrame) return startFrame;
        if (frame > endFrame)   return endFrame;
        return frame;
    }

    friend constexpr bool operator==(const PlaybackRange&, const PlaybackRange&) = default;
};

} // namespace atk::timeline
