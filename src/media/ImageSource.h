#pragma once

#include "media/MediaMetadata.h"

#include <QMetaType>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace atk::media {

/// How an image source -- a single still or a numbered image sequence -- is
/// presented as a timeline source.
///
/// A still has no duration of its own, so it is held for `holdFrames` frames.
/// A sequence is one picture per frame from `sequenceFirst` to `sequenceLast`
/// (inclusive source frame numbers). Both play at `frameRate`.
///
/// These options belong to the *source*, not to the application: bookmarks and
/// review ranges are frame indices and timing, so if they followed a global
/// preference, changing that preference would silently re-time or invalidate
/// saved review state. Preferences only seed newly added sources; see
/// docs/STILL_IMAGE_SOURCES.md.
struct ImageSourceOptions {
    /// TimelineViewport's minimum span. A shorter hold could not satisfy the
    /// review range's own invariant.
    static constexpr int64_t kMinimumHoldFrames = 10;
    static constexpr int64_t kMaximumHoldFrames = 100'000;
    static constexpr int64_t kDefaultHoldFrames = 48;
    static constexpr FrameRate kDefaultFrameRate{ 24, 1 };
    /// Upper bound on a sequence's frame-number span, so a stray file named
    /// frame_9999999.png cannot turn a sequence into millions of held gaps.
    static constexpr int64_t kMaximumSequenceFrames = 1'000'000;

    int64_t holdFrames = kDefaultHoldFrames;
    FrameRate frameRate = kDefaultFrameRate;
    /// Inclusive source frame numbers of an image sequence; -1 for a still.
    int64_t sequenceFirst = -1;
    int64_t sequenceLast = -1;

    bool isSequence() const { return sequenceFirst >= 0 && sequenceLast >= sequenceFirst; }
    int64_t sequenceLength() const { return isSequence() ? sequenceLast - sequenceFirst + 1 : 0; }

    bool isValid() const
    {
        if (!frameRate.isValid()) return false;
        if (sequenceFirst >= 0 || sequenceLast >= 0) {
            return isSequence() && sequenceLength() <= kMaximumSequenceFrames;
        }
        return holdFrames >= kMinimumHoldFrames && holdFrames <= kMaximumHoldFrames;
    }

    /// Clamps the hold into range and replaces an invalid rate with the
    /// default, so a decoder never synthesizes an unusable extent. A malformed
    /// sequence range is dropped (the source then behaves as a still).
    ImageSourceOptions normalized() const;

    friend bool operator==(const ImageSourceOptions&, const ImageSourceOptions&) = default;
};

/// The rates offered for image sources: 23.976, 24, 25, 29.97, 30, 48, 50,
/// 59.94 and 60, with the broadcast rates as exact 1001 rationals.
const QList<FrameRate>& imageFrameRatePresets();

/// "23.976 fps", "24 fps" -- the label used wherever a preset is offered.
QString frameRateLabel(const FrameRate& rate);

/// Lower-case extensions, without dots, that are opened as images. This is the
/// one list the decoder, the file dialogs, drag and drop and the project
/// serializer share, so they cannot disagree.
const QStringList& stillImageExtensions();

/// True when `filePath` has an image extension (case-insensitive). Also true
/// for a sequence pattern such as "shot.%04d.exr".
///
/// Classification is by extension on purpose: it decides, before anything is
/// opened, that a file is demuxed as exactly one picture (image2 with
/// pattern_type=none) rather than probed, which is what keeps names containing
/// digits or '%' from being read as sequence patterns. Sequences are only ever
/// opened explicitly, through ImageSourceOptions.
bool isStillImagePath(const QString& filePath);

/// A numbered image sequence found on disk.
///
/// `pattern` is the full path with the frame number replaced by a printf
/// token: "%0Nd" when the numbers are zero-padded to N digits, "%d" when they
/// are not. Frame numbers are the numbers in the file names (1001, not 0).
struct ImageSequence {
    QString pattern;
    int64_t first = -1;
    int64_t last = -1;
    /// Files actually present between first and last.
    int64_t presentCount = 0;

    bool isValid() const { return !pattern.isEmpty() && first >= 0 && last > first; }
    int64_t length() const { return isValid() ? last - first + 1 : 0; }
    int64_t missingCount() const { return length() - presentCount; }
};

/// Finds the sequence `filePath` belongs to: files in the same directory that
/// differ only in the last run of digits of the name. Returns nothing when the
/// file has no frame number or has no numbered neighbours, so a lone numbered
/// still is never mistaken for a sequence.
std::optional<ImageSequence> detectImageSequence(const QString& filePath);

/// The file for `frameNumber` of `pattern` ("shot.%04d.exr", 7 -> "shot.0007.exr").
/// Returns an empty string when `pattern` has no frame token.
QString sequenceFramePath(const QString& pattern, int64_t frameNumber);

/// Source frame numbers of `pattern` that exist on disk within [first, last],
/// in ascending order. One directory listing, not one stat per frame.
QList<int64_t> presentSequenceFrames(const QString& pattern, int64_t first, int64_t last);

/// Human-readable sequence name: "shot.[1001-1096].exr".
QString sequenceDisplayName(const QString& pattern, int64_t first, int64_t last);

/// The pattern's file name with the frame token and any separator before it
/// removed ("shot.%04d.exr" -> "shot"), for naming exports.
QString sequenceBaseName(const QString& pattern);

/// Whether a source's media is on disk. A still or video must exist as a file;
/// a sequence needs at least one frame of its range present.
bool imageSourceExists(const QString& path, const ImageSourceOptions& options);

} // namespace atk::media

Q_DECLARE_METATYPE(atk::media::ImageSourceOptions)
