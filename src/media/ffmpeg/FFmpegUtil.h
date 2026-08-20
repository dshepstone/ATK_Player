#pragma once

#include <QString>

#include <cstdint>

extern "C" {
#include <libavutil/rational.h>
}

// ---------------------------------------------------------------------------
// Shared FFmpeg helpers.
//
// One place for error translation and rational/time-base arithmetic, so those
// two things are never reimplemented per call site. Everything here is pure and
// thread-safe.
// ---------------------------------------------------------------------------

namespace atk::media::ffmpeg {

/// Translates an FFmpeg error code into readable text via av_strerror().
///
/// FFmpeg returns negative packed error codes. Logging a bare -1094995529 tells
/// nobody anything, so every failure path goes through this.
QString errorString(int errorCode);

/// "<context>: <message> (code)" -- e.g.
/// "avformat_open_input: No such file or directory (-2)".
QString errorString(const char* context, int errorCode);

/// Library versions, for the startup banner.
QString libraryVersions();

// --- Time-base conversion --------------------------------------------------
//
// All conversions go through av_rescale_q so timing stays exact rational
// arithmetic. Converting to double and back accumulates error, which on a long
// clip eventually lands the playhead on the wrong frame -- the one thing an
// animation review tool must never do.

/// Stream timestamp -> microseconds.
int64_t ptsToMicroseconds(int64_t pts, AVRational timeBase);

/// Microseconds -> stream timestamp.
int64_t microsecondsToPts(int64_t microseconds, AVRational timeBase);

/// Zero-based frame index -> stream timestamp, given the stream start time.
int64_t frameIndexToPts(int64_t frameIndex, AVRational frameRate,
                        AVRational timeBase, int64_t startTime);

/// Stream timestamp -> zero-based frame index. Rounds to nearest, so a
/// timestamp a hair off the ideal grid still resolves to the intended frame.
int64_t ptsToFrameIndex(int64_t pts, AVRational frameRate,
                        AVRational timeBase, int64_t startTime);

/// Frame index -> microseconds from the start of the media.
int64_t frameIndexToMicroseconds(int64_t frameIndex, AVRational frameRate);

/// Microseconds from the start of the media -> frame index.
int64_t microsecondsToFrameIndex(int64_t microseconds, AVRational frameRate);

/// True when the rational is usable as a frame rate or time base.
bool isValidRational(AVRational rational);

} // namespace atk::media::ffmpeg
