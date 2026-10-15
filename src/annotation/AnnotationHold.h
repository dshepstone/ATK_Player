#pragma once

#include "annotation/AnnotationTypes.h"

#include <QString>
#include <QVector>

namespace atk::annotation {

/// Held drawings, like SyncSketch's Hold Sketches: a drawing stays on screen
/// on the frames after it until the next drawn frame, the way an animator's
/// key drawing holds until the next one. A view setting only: it never
/// changes annotation data.
enum class HoldMode {
    Off,          ///< drawings show on their own frame only
    AddToCurrent, ///< drawing on a held frame adds to the held drawing
    AddNew,       ///< drawing on a held frame starts a new drawing there
};

/// Stable names for settings ("off", "addToCurrent", "addNew").
QString holdModeKey(HoldMode mode);
/// The mode for `key`; unknown keys give `fallback`.
HoldMode holdModeFromKey(const QString& key, HoldMode fallback = HoldMode::Off);

/// The frame whose drawings are on screen at `frame` with holding on: `frame`
/// itself when it has drawings, otherwise the nearest earlier frame with
/// drawings. -1 when there is none or `frame` is negative. Frames with only
/// written notes never hold.
int64_t heldDrawingFrame(const SourceAnnotations& annotations, int64_t frame);

/// One held drawing on the timeline: drawn on `first`, on screen through
/// `last` (inclusive).
struct HoldSpan {
    int64_t first = 0;
    int64_t last = 0;

    friend bool operator==(const HoldSpan&, const HoldSpan&) = default;
};

/// Every drawn frame with the frames it holds over, up to `lastFrame`
/// (inclusive). A drawing directly followed by another drawing holds over
/// nothing and has `first == last`.
QVector<HoldSpan> holdSpans(const SourceAnnotations& annotations, int64_t lastFrame);

} // namespace atk::annotation
