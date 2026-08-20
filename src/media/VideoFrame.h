#pragma once

#include <QImage>

#include <cstdint>

namespace atk::media {

/// One decoded picture, ready for display.
///
/// Phase 0 / M1 use QImage so the viewer can blit directly with no extra
/// machinery. When GPU upload and higher bit depths arrive (M2+), this type
/// gains a device-side buffer and QImage becomes the CPU fallback; keeping the
/// wrapper now means the viewer and cache signatures will not change then.
struct VideoFrame {
    /// Zero-based frame index within the source.
    int64_t frameNumber = -1;
    /// Presentation timestamp in microseconds from the start of the source.
    int64_t presentationTimeUs = -1;
    QImage image;

    bool isValid() const { return frameNumber >= 0 && !image.isNull(); }

    /// Approximate memory footprint, used by FrameCache to enforce its budget.
    qsizetype sizeInBytes() const { return image.sizeInBytes(); }
};

} // namespace atk::media
