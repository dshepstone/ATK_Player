#pragma once

namespace atk::exporter {

/// What an export draws over the picture: the "Drawings, notes and burn-ins"
/// choices. A review keeps the last ones it was exported with (see
/// project::Project::exportBurnIns()).
struct ExportBurnIns {
    bool frameNumber = false;
    bool bookmarkLabels = false;
    bool bookmarkNotes = false;
    /// Drawings made on frames, burned in on the frame they were drawn on.
    bool annotations = false;
    /// Frame notes from the Notes panel, as a caption on their frame.
    bool annotationNotes = false;
    /// Keep drawings and notes on screen for this many further frames (or
    /// until the next annotated frame), so they can be read in a play-through.
    /// 0 shows each exactly on its own frame.
    int annotationHoldFrames = 0;
    /// Video only: stop the picture on each drawn or noted frame for this many
    /// further frames (the sound pauses too), then play on. 0 = no freeze.
    int annotationFreezeFrames = 0;
    /// Tracing paper, as in the viewer: a white wash over the picture and
    /// under the drawings, in percent (0 = off).
    int tracingPaperPercent = 0;

    bool enabled() const
    {
        return frameNumber || bookmarkLabels || bookmarkNotes || annotations || annotationNotes
            || tracingPaperPercent > 0;
    }

    friend bool operator==(const ExportBurnIns&, const ExportBurnIns&) = default;
};

} // namespace atk::exporter
