#pragma once

#include <QImage>

#include <cstdint>

namespace atk::media {

/// One decoded picture, converted to a display-ready format.
///
/// The conversion from the decoder's native pixel format happens in the media
/// layer (via libswscale), never in the viewer: `ViewerWidget` receives an image
/// it can blit directly, so no decoding or format conversion can end up inside
/// a paint event.
///
/// `frameIndex` and `ptsTicks` are two views of the same position. The index is
/// what the user navigates by and what the UI shows; the ticks are the exact
/// stream timestamp the frame was decoded with. Keeping both means the index can
/// be derived by exact rational arithmetic rather than by counting frames, which
/// is what makes stepping survive a seek.
struct VideoFrame {
    /// Which open-media generation produced this frame.
    ///
    /// Travels with the frame so a late arrival can be matched against the
    /// currently open file. Without it, a frame decoded from the previous
    /// source is indistinguishable from a current one once it reaches the UI.
    /// See DecodeGeneration.h.
    uint64_t sourceGeneration = 0;

    /// Zero-based frame index within the source. -1 when unset.
    int64_t frameIndex = -1;
    /// Presentation timestamp in the video stream's own time base.
    int64_t ptsTicks = 0;
    /// Presentation timestamp in microseconds from the start of the media.
    int64_t ptsUs = 0;

    int width = 0;
    int height = 0;

    /// Display-ready image. QImage::Format_RGB32 for M1 -- 32-bit aligned, and
    /// what QPainter blits fastest without a further conversion.
    QImage image;

    bool isValid() const { return frameIndex >= 0 && !image.isNull(); }

    /// Presentation time in seconds.
    double ptsSeconds() const { return static_cast<double>(ptsUs) / 1'000'000.0; }

    /// Approximate memory footprint, used by FrameCache to enforce its budget.
    qsizetype sizeInBytes() const { return image.sizeInBytes(); }
};

} // namespace atk::media
