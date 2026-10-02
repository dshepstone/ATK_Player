#pragma once

#include "media/MediaMetadata.h"

#include <QMetaType>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace atk::media {

/// How a still image is presented as a timeline source.
///
/// A single picture has no duration of its own, so it is held for a fixed
/// number of frames at a rational rate. The hold belongs to the *source*, not to
/// the application: bookmarks and review ranges are frame indices, and if the
/// extent followed a global preference, changing that preference would silently
/// invalidate saved review state. The preference only seeds newly added stills;
/// see docs/STILL_IMAGE_SOURCES.md.
struct StillImageOptions {
    /// TimelineViewport's minimum span. A shorter hold could not satisfy the
    /// review range's own invariant.
    static constexpr int64_t kMinimumHoldFrames = 10;
    static constexpr int64_t kMaximumHoldFrames = 100'000;
    static constexpr int64_t kDefaultHoldFrames = 48;
    static constexpr FrameRate kDefaultFrameRate{ 24, 1 };

    int64_t holdFrames = kDefaultHoldFrames;
    FrameRate frameRate = kDefaultFrameRate;

    bool isValid() const
    {
        return holdFrames >= kMinimumHoldFrames && holdFrames <= kMaximumHoldFrames
            && frameRate.isValid();
    }

    /// Clamps the hold into range and replaces an invalid rate with the
    /// default, so a decoder never synthesizes an unusable extent.
    StillImageOptions normalized() const;

    friend bool operator==(const StillImageOptions&, const StillImageOptions&) = default;
};

/// Lower-case extensions, without dots, that are opened as single still
/// images. This is the one list the decoder, the file dialogs, drag and drop
/// and the project serializer share, so they cannot disagree.
const QStringList& stillImageExtensions();

/// True when `filePath` has a still-image extension (case-insensitive).
///
/// Classification is by extension on purpose: it decides, before anything is
/// opened, that the file is demuxed as exactly one picture (image2 with
/// pattern_type=none) rather than probed, which is what keeps names containing
/// digits or '%' from being read as sequence patterns.
bool isStillImagePath(const QString& filePath);

} // namespace atk::media

Q_DECLARE_METATYPE(atk::media::StillImageOptions)
