#include "media/MediaMetadata.h"

namespace atk::media {

int64_t MediaMetadata::effectiveFrameCount() const
{
    if (frameCount >= 0) {
        return frameCount;
    }
    if (durationUs >= 0 && frameRate.isValid()) {
        // durationUs * (num / den) / 1'000'000, in integer arithmetic to avoid
        // rounding a long clip off by a frame.
        const auto num = static_cast<int64_t>(frameRate.numerator);
        const auto den = static_cast<int64_t>(frameRate.denominator);
        return (durationUs * num) / (den * 1'000'000);
    }
    return -1;
}

} // namespace atk::media
