#pragma once

#include "annotation/AnnotationTypes.h"

#include <QPointF>
#include <QPolygonF>
#include <QSizeF>

namespace atk::annotation {

/// Free transform (the Select tool's handles), like Photoshop's: scale an
/// item by its box, rotate it, or drag a line's ends.
///
/// All geometry here is in *picture space*: the source picture laid out as
/// `imageSize` (the viewer passes its on-screen picture rect, an export would
/// pass the frame size), y down, angles in degrees clockwise on screen. Using
/// the displayed proportions rather than raw 0..1 coordinates keeps a
/// rotation rigid on a picture that is not square.
///
/// Pure geometry, no widgets, so it lives in atk_core and is tested headless.

/// A rectangle of `size` centred on `center`, turned `angle` degrees about it.
struct TransformBox {
    QPointF center;
    QSizeF size;
    qreal angle = 0.0;

    /// Picture point for a point in the box's own (unrotated, centred) frame.
    QPointF toPicture(const QPointF& local) const;
    QPointF toLocal(const QPointF& picture) const;
    /// Corners clockwise from top-left, in picture space.
    QPolygonF polygon() const;
    /// Handle position: 0..7 clockwise from top-left (corners and edge midpoints).
    QPointF handlePoint(int handle) const;

    friend bool operator==(const TransformBox&, const TransformBox&) = default;
};

/// What a press on the selection grabs.
enum TransformHandle : int {
    kNoHandle = -1,
    // 0..7: box handles clockwise from top-left (corners even, edges odd).
    kRotateHandle = 8,
    kStartPointHandle = 9, // Line / Arrow first end
    kEndPointHandle = 10,  // Line / Arrow second end
};

/// Which handles an item offers. Lines and arrows are edited by their two ends;
/// a single dab has nothing to scale; text scales by its corners only (it keeps
/// its proportions) and does not rotate.
bool hasBoxHandles(const AnnotationItem& item);
bool hasEdgeHandles(const AnnotationItem& item);
bool canRotate(const AnnotationItem& item);
bool hasEndPointHandles(const AnnotationItem& item);

/// The box the handles sit on. Freehand strokes get their points' bounds,
/// padded by the brush radius so the box surrounds the ink; shapes get their
/// own outline and rotation; text gets its laid-out block.
TransformBox transformBox(const AnnotationItem& item, const QSizeF& imageSize);

struct TransformOptions {
    bool keepAspect = false; // Shift on a corner (always on for text)
    bool fromCenter = false; // Alt: opposite side moves too
    bool snapAngle = false;  // Shift while rotating: 15 degree steps
    qreal minimumSize = 6.0; // picture-space units
};

/// `start` after dragging box `handle` (0..8) from `press` to `current`
/// (picture space). Edges stay square to the box, so a rotated box scales
/// along its own sides.
TransformBox draggedBox(const TransformBox& start, int handle, const QPointF& press, const QPointF& current,
                        const TransformOptions& options);

/// `item` carried from box `from` to box `to`: every point keeps its place
/// relative to the box. Freehand strokes, lines and arrows take any rotation
/// into their points; rectangles and ellipses store it in `rotation`. Line
/// widths and brush sizes are unchanged; text scales its type with the box.
/// Points are kept on the picture.
AnnotationItem transformedItem(const AnnotationItem& item, const QSizeF& imageSize, const TransformBox& from,
                               const TransformBox& to);

/// A line or arrow with one end (kStartPointHandle / kEndPointHandle) moved
/// to `normalized`, kept on the picture.
AnnotationItem movedEndPoint(const AnnotationItem& item, int handle, const QPointF& normalized);

} // namespace atk::annotation
