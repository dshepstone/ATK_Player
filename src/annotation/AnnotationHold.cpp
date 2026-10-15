#include "annotation/AnnotationHold.h"

#include <algorithm>

namespace atk::annotation {

QString holdModeKey(HoldMode mode)
{
    switch (mode) {
    case HoldMode::Off:          return QStringLiteral("off");
    case HoldMode::AddToCurrent: return QStringLiteral("addToCurrent");
    case HoldMode::AddNew:       return QStringLiteral("addNew");
    }
    return QStringLiteral("off");
}

HoldMode holdModeFromKey(const QString& key, HoldMode fallback)
{
    for (const HoldMode mode : {HoldMode::Off, HoldMode::AddToCurrent, HoldMode::AddNew})
        if (key == holdModeKey(mode)) return mode;
    return fallback;
}

int64_t heldDrawingFrame(const SourceAnnotations& annotations, int64_t frame)
{
    if (frame < 0) return -1;
    const auto& frames = annotations.frames();
    // Walk back from the first frame after `frame`, skipping notes-only frames.
    auto it = frames.upperBound(frame);
    while (it != frames.cbegin()) {
        --it;
        if (it->hasDrawings()) return it.key();
    }
    return -1;
}

QVector<HoldSpan> holdSpans(const SourceAnnotations& annotations, int64_t lastFrame)
{
    QVector<HoldSpan> spans;
    if (lastFrame < 0) return spans;
    for (const FrameAnnotation& frame : annotations.frames()) {
        if (!frame.hasDrawings() || frame.frame < 0 || frame.frame > lastFrame) continue;
        if (!spans.isEmpty()) spans.last().last = std::max(spans.last().first, frame.frame - 1);
        spans.append({frame.frame, lastFrame});
    }
    return spans;
}

} // namespace atk::annotation
