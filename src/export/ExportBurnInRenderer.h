#pragma once

#include "export/ExportSpec.h"
#include <QImage>
#include <QRect>

namespace atk::exporter {

class ExportBurnInRenderer final {
public:
    static QString frameNumberText(qint64 sourceAFrameIndex);
    static QString bookmarkTitle(const ExportBookmark& bookmark);
    static QString bookmarkFrameText(const ExportBookmark& bookmark);
    static QVector<ExportBookmark> activeBookmarks(const ExportSpec& spec, qint64 sourceAFrameIndex);
    static QRect frameNumberBounds(const QSize& imageSize);
    static QRect bookmarkBounds(const QSize& imageSize);
    /// Drawn/noted frame shown on `sourceAFrameIndex`: its own, or the latest
    /// one within the hold. nullptr when none applies or annotations are off.
    static const annotation::FrameAnnotation* annotationFor(const ExportSpec& spec, qint64 sourceAFrameIndex);
    static QRect noteBounds(const QSize& imageSize);
    /// True when the video should freeze on `sourceAFrameIndex`: it is itself a
    /// drawn or noted frame that is being burned in, and a freeze is set.
    static bool freezesOn(const ExportSpec& spec, qint64 sourceAFrameIndex);
    /// One entry per output video frame: the source frame shown, and whether
    /// it repeats the previous one (a freeze). Without freezes it is simply the
    /// export range.
    struct ScheduledFrame {
        qint64 sourceFrame = 0;
        bool repeat = false;
    };
    static QVector<ScheduledFrame> videoSchedule(const ExportSpec& spec);
    /// Splices silence into `sourcePcm` (the range's sound, frame-grid aligned)
    /// wherever the schedule repeats a frame, so sound stays in step with the
    /// picture after each freeze. Interleaved PCM, `bytesPerSample` per frame
    /// of all channels.
    static QByteArray scheduleAudio(const QByteArray& sourcePcm, const QVector<ScheduledFrame>& schedule,
                                    media::FrameRate rate, int sampleRate, int bytesPerSample);
    /// Samples from the start of the range to the start of frame `n`, on the
    /// exact rational frame grid.
    static qint64 samplesAtFrame(qint64 n, media::FrameRate rate, int sampleRate);
    static void apply(QImage& image, const ExportSpec& spec, qint64 sourceAFrameIndex);
};

} // namespace atk::exporter
