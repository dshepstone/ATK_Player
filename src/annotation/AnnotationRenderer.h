#pragma once

#include "annotation/AnnotationTypes.h"

#include <QFont>
#include <QImage>
#include <QPointF>
#include <QRectF>

class QPainter;

namespace atk::annotation {

/// Draws annotations. The one renderer used by the viewer and by every export,
/// so what a reviewer draws is exactly what ends up in a PDF or MP4.
///
/// Pure QPainter/QImage (Qt Gui only), so it lives in atk_core and is tested
/// headless. It never decodes or knows about widgets.
///
/// `imageRect` is where the source picture occupies the painter's coordinate
/// space; normalised item coordinates map into it, and brush sizes scale with
/// its height. The caller handles flip/zoom by choosing the rect and the
/// painter transform, exactly as it does for the picture.
///
/// Freehand strokes are rendered into a per-item layer one segment at a time
/// with Source composition, then composited once. That gives:
///   - variable width and opacity from pressure along the stroke,
///   - no darkening where a stroke crosses itself (each pixel is written once
///     per stroke), so a translucent marker looks like a marker,
///   - pencil grain applied to the whole stroke from a deterministic seed.
class AnnotationRenderer {
public:
    /// Paints every item of `frame` in order.
    static void paintFrame(QPainter& painter, const QRectF& imageRect, const FrameAnnotation& frame);
    static void paintItem(QPainter& painter, const QRectF& imageRect, const AnnotationItem& item);

    /// Convenience for exporters: paints over `image` filling its full rect.
    static void renderOnto(QImage& image, const FrameAnnotation& frame);

    /// Brush radius in painter pixels for a point with `pressure`, before
    /// tool shaping. Exposed for the brush-size cursor and tests.
    static qreal baseRadius(const QRectF& imageRect, const AnnotationItem& item);

    /// Radius of a freehand item's ink at `pressure`, as drawn (tool shaping
    /// included). Shared with the area eraser so it cuts exactly what is visible.
    static qreal inkRadius(const QRectF& imageRect, const AnnotationItem& item, qreal pressure);

    /// Painter-space bounds of an item including its stroke width.
    static QRectF itemBounds(const QRectF& imageRect, const AnnotationItem& item);

    /// True when `position` (painter space) lies on the item within
    /// `tolerance` pixels. Used by the stroke eraser.
    static bool hitTest(const QRectF& imageRect, const AnnotationItem& item, const QPointF& position,
                        qreal tolerance);

    /// Smoothed centreline used for freehand rendering (Catmull-Rom through the
    /// stored samples, pressure interpolated). Painter space. Exposed for tests.
    struct PathSample {
        QPointF position;
        qreal pressure = 1.0;
    };
    static QVector<PathSample> smoothedPath(const QRectF& imageRect, const AnnotationItem& item);

    /// Text layout shared by painting, hit-testing and the in-place editor, so
    /// what is typed lines up with what is drawn. Point text keeps its lines as
    /// typed; box text wraps at the box width (words first, then anywhere).
    struct TextLine {
        QString text;
        QPointF baseline; // painter space
        qreal width = 0.0;
    };
    struct TextBlock {
        QFont font;
        QRectF bounds; // painter space; box text is the box width wide
        QVector<TextLine> lines;
    };
    static QFont textFont(const QRectF& imageRect, const AnnotationItem& item);
    static TextBlock layoutText(const QRectF& imageRect, const AnnotationItem& item);

    /// Deterministic grain value in [0,1) for pixel (x,y) and a seed.
    static float grain(int x, int y, quint32 seed);
};

} // namespace atk::annotation
