#pragma once

#include "timeline/PlaybackRange.h"

#include <QString>

namespace atk::exporter {

/// What an export produces.
enum class ExportFormat {
    /// Re-encoded video via FFmpeg (H.264 in MP4 by default).
    Video,
    /// Numbered still images, e.g. review_0001.png.
    ImageSequence,
    /// A single still of the current frame.
    SingleFrame,
};

/// Parameters for one export.
struct ExportSettings {
    ExportFormat format = ExportFormat::Video;
    QString outputPath;
    /// Frames to export. When disabled, the whole source is exported.
    timeline::PlaybackRange range;
    /// Draw bookmarks, frame numbers and notes into the output.
    bool burnInAnnotations = false;
    /// 0 = source resolution.
    int outputWidth = 0;
    int outputHeight = 0;
};

/// Runs an export.
///
/// PHASE 0 STATUS: declaration only. Implemented in milestone M5, on top of the
/// same FFmpeg libraries as playback -- LGPL components only, so encoders that
/// require GPL or nonfree builds are not offered.
///
/// The export runs off the UI thread and reports progress; it must never reuse
/// the playback decoder instance, because scrubbing during an export would
/// otherwise fight the export for the decoder's seek position.
class FFmpegExporter {
public:
    /// Validates settings without starting anything. Returns an empty string
    /// when the settings are usable, or a reason they are not.
    static QString validate(const ExportSettings& settings);

    /// TODO(M5): start the export on a worker thread and report progress.
    static bool isSupported();
};

} // namespace atk::exporter
