#pragma once

#include <QSize>
#include <QString>

#include <cstdint>

namespace atk::media {

/// Exact frame rate as a rational number.
///
/// Broadcast rates such as 23.976 and 29.97 are exactly 24000/1001 and
/// 30000/1001. Storing them as a double loses that exactness and makes
/// frame <-> timecode conversion drift on long clips, so the rate is kept
/// rational throughout and converted to a double only for display.
struct FrameRate {
    int numerator = 0;
    int denominator = 1;

    constexpr bool isValid() const { return numerator > 0 && denominator > 0; }

    constexpr double toDouble() const
    {
        return isValid() ? static_cast<double>(numerator) / denominator : 0.0;
    }

    friend constexpr bool operator==(const FrameRate&, const FrameRate&) = default;

    static constexpr FrameRate fromInteger(int fps) { return { fps, 1 }; }
};

/// Everything ATK Player knows about a media file after probing it.
///
/// Populated by a decoder (see media/decoders/IDecoder.h). All fields keep
/// their default until a real decoder fills them in, so UI code must handle
/// the "not yet probed" state rather than assuming valid values.
struct MediaMetadata {
    QString filePath;
    QString containerFormat;   ///< e.g. "mov,mp4,m4a,3gp"
    QString videoCodecName;    ///< e.g. "h264"
    QString audioCodecName;    ///< empty when the file has no audio

    QSize resolution;          ///< coded picture size in pixels
    double pixelAspectRatio = 1.0;

    FrameRate frameRate;
    /// Total video frames. -1 when unknown (some containers only give duration).
    int64_t frameCount = -1;
    /// Duration in microseconds. -1 when unknown.
    int64_t durationUs = -1;

    bool hasVideo = false;
    bool hasAudio = false;
    int audioSampleRate = 0;
    int audioChannelCount = 0;

    bool isValid() const { return hasVideo || hasAudio; }

    /// Frame count derived from duration when the container does not report it
    /// directly. Returns -1 if neither is known.
    int64_t effectiveFrameCount() const;
};

} // namespace atk::media
