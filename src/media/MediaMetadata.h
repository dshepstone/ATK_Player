#pragma once

#include <QSize>
#include <QString>
#include <QMetaType>

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

/// A rational time base, mirroring FFmpeg's AVRational without dragging an
/// FFmpeg header into the model layer.
struct TimeBase {
    int numerator = 0;
    int denominator = 1;

    constexpr bool isValid() const { return numerator > 0 && denominator > 0; }

    friend constexpr bool operator==(const TimeBase&, const TimeBase&) = default;
};

/// Where a total frame count came from, so the UI can be honest about it.
///
/// Containers vary wildly in what they report. Claiming an estimate is exact
/// would make the last frame of a clip unreachable or overshoot the end, and
/// the user would have no way of knowing why.
enum class FrameCountSource {
    /// No count is available at all.
    Unknown,
    /// The container reported nb_frames and it looked trustworthy.
    StreamMetadata,
    /// Derived from duration x frame rate. Correct for constant-frame-rate
    /// material, approximate otherwise.
    EstimatedFromDuration,
    /// Established by actually decoding to the end.
    Counted,
};

/// Everything ATK Player knows about a media file after probing it.
///
/// Populated by MediaDecoder from FFmpeg stream information. This struct stays
/// free of FFmpeg types so the timeline, the UI and the tests can use it
/// without linking or including any of libav*.
struct MediaMetadata {
    // --- Identity ---------------------------------------------------------
    QString filePath;          ///< Full path as opened
    QString fileName;          ///< Base name, for the sources panel and title
    QString containerFormat;   ///< e.g. "mov,mp4,m4a,3gp,3g2,mj2"
    QString containerLongName; ///< Human-readable container description

    // --- Video ------------------------------------------------------------
    bool hasVideo = false;
    int videoStreamIndex = -1;
    QString videoCodecName;     ///< e.g. "h264"
    QString videoCodecLongName;
    QString pixelFormatName;    ///< source pixel format, e.g. "yuv420p"
    QSize resolution;           ///< coded picture size in pixels
    double pixelAspectRatio = 1.0;
    /// Display rotation in degrees from container side data (0, 90, 180, 270).
    int rotationDegrees = 0;

    /// Exact rational frame rate, from av_guess_frame_rate().
    FrameRate frameRate;
    /// The video stream's time base, needed to convert PTS values.
    TimeBase videoTimeBase;
    /// First presentation timestamp of the video stream, in its own time base.
    /// Not always zero -- some containers start elsewhere, and ignoring it puts
    /// every computed frame index off by a constant.
    int64_t videoStartTime = 0;

    // --- Audio ------------------------------------------------------------
    bool hasAudio = false;
    int audioStreamIndex = -1;
    QString audioCodecName;
    QString audioCodecLongName;
    int audioSampleRate = 0;
    int audioChannelCount = 0;
    QString audioChannelLayout; ///< e.g. "stereo", "5.1"
    TimeBase audioTimeBase;

    // --- Extent -----------------------------------------------------------
    /// Duration in microseconds. -1 when unknown.
    int64_t durationUs = -1;
    /// Total video frames. -1 when unknown.
    int64_t frameCount = -1;
    FrameCountSource frameCountSource = FrameCountSource::Unknown;

    /// True when the file carries something ATK Player can present.
    bool isValid() const { return hasVideo || hasAudio; }

    /// True when frameCount is known to be exact rather than derived.
    bool hasExactFrameCount() const
    {
        return frameCountSource == FrameCountSource::StreamMetadata
            || frameCountSource == FrameCountSource::Counted;
    }

    /// Last valid frame index, or -1 when there is no count.
    int64_t lastFrameIndex() const { return frameCount > 0 ? frameCount - 1 : -1; }

    /// Frame count derived from duration when the container does not report it
    /// directly. Returns -1 if neither is known.
    int64_t effectiveFrameCount() const;

    /// Duration in seconds, for display. -1 when unknown.
    double durationSeconds() const;

    /// One-line summary for the sources panel, e.g. "1920x1080 - 23.976 fps".
    QString shortDescription() const;
};

} // namespace atk::media

Q_DECLARE_METATYPE(atk::media::MediaMetadata)
