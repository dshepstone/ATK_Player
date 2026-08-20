#pragma once

#include "media/MediaMetadata.h"

#include <QString>

#include <cstdint>

namespace atk::timeline {

/// Frame <-> SMPTE timecode conversion.
///
/// Non-drop-frame only. For 23.976 / 29.97 / 59.94 material the displayed
/// timecode therefore drifts from wall-clock time, which is what animation
/// review wants: frame numbers stay contiguous and match the DCC scene.
/// Drop-frame display is a milestone M2 option, not a default.
namespace timecode {

/// Formats a frame as "HH:MM:SS:FF". Returns "--:--:--:--" when the frame rate
/// is unknown, so the status bar has a defined empty state.
QString fromFrame(int64_t frame, media::FrameRate rate);

/// Parses "HH:MM:SS:FF". Returns -1 when the string or rate is invalid.
int64_t toFrame(const QString& text, media::FrameRate rate);

/// The string shown when no media is loaded.
QString placeholder();

} // namespace timecode
} // namespace atk::timeline
