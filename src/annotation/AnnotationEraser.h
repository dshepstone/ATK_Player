#pragma once

#include "annotation/AnnotationTypes.h"

#include <QPointF>
#include <QSizeF>

namespace atk::annotation {

/// How the eraser treats what it touches.
enum class EraserMode {
    /// Removes only the ink under the eraser; a crossed stroke is split into
    /// the pieces either side. Shapes and text, which have no meaningful
    /// pieces, are removed whole.
    Area,
    /// Removes every item the eraser touches.
    WholeStroke,
};

/// Result of erasing one circle from one item.
struct EraseResult {
    bool changed = false;
    /// What remains when changed: zero pieces (fully erased) or more. Piece ids
    /// are 0; the store assigns them.
    QVector<AnnotationItem> pieces;
};

/// Erases a disc from an item.
///
/// Geometry is in source-image pixels: `imageSize` is the picture's pixel size,
/// `center` is normalised (0..1), `radius` is in image pixels. A freehand
/// stroke is resampled along the same smoothed path the renderer draws, and a
/// sample is removed when its ink -- not just its centreline -- reaches into the
/// disc, so no visible ink is left inside the eraser.
///
/// Pure function in atk_core; the AnnotationStore applies results as one undoable
/// edit, and the viewer only reports where the eraser went.
EraseResult eraseDisc(const AnnotationItem& item, const QSizeF& imageSize, const QPointF& center,
                      qreal radius, EraserMode mode);

} // namespace atk::annotation
