#include "annotation/AnnotationEraser.h"

#include "annotation/AnnotationRenderer.h"

#include <algorithm>
#include <cmath>

namespace atk::annotation {
namespace {

AnnotationItem pieceFrom(const AnnotationItem& source, const QVector<StrokePoint>& points)
{
    AnnotationItem piece = source;
    piece.id = 0;
    if (points.size() <= AnnotationLimits::kMaxPointsPerItem) {
        piece.points = points;
        return piece;
    }
    // Keep within the per-item limit by even decimation, always keeping both ends.
    piece.points.clear();
    const int count = AnnotationLimits::kMaxPointsPerItem;
    for (int i = 0; i < count; ++i)
        piece.points.append(points[int(qint64(i) * (points.size() - 1) / (count - 1))]);
    return piece;
}

} // namespace

EraseResult eraseDisc(const AnnotationItem& item, const QSizeF& imageSize, const QPointF& center,
                      qreal radius, EraserMode mode)
{
    EraseResult result;
    if (!isValidItem(item) || imageSize.width() <= 0 || imageSize.height() <= 0 || !(radius > 0.0))
        return result;
    const QRectF rect(QPointF(0, 0), imageSize);
    const QPointF c(center.x() * imageSize.width(), center.y() * imageSize.height());

    // Cheap rejection before any per-sample work.
    const QRectF reach = AnnotationRenderer::itemBounds(rect, item).adjusted(-radius, -radius, radius, radius);
    if (!reach.contains(c)) return result;

    if (mode == EraserMode::WholeStroke || !isFreehandTool(item.tool)) {
        if (AnnotationRenderer::hitTest(rect, item, c, radius)) result.changed = true; // pieces empty
        return result;
    }

    // The renderer's smoothed path, filled in so no two samples are further
    // apart than a quarter of the eraser: a quick straight stroke may be stored
    // as just its two ends, and the eraser must still be able to cut between them.
    const QVector<AnnotationRenderer::PathSample> smoothed = AnnotationRenderer::smoothedPath(rect, item);
    const qreal spacing = std::max<qreal>(0.5, radius * 0.25);
    QVector<AnnotationRenderer::PathSample> samples;
    samples.reserve(smoothed.size());
    for (int i = 0; i < smoothed.size(); ++i) {
        if (i > 0) {
            const auto& a = smoothed[i - 1];
            const auto& b = smoothed[i];
            const qreal length = std::hypot(b.position.x() - a.position.x(), b.position.y() - a.position.y());
            const int steps = std::min(4096, int(std::ceil(length / spacing)));
            for (int s = 1; s < steps; ++s) {
                const qreal t = qreal(s) / steps;
                samples.append({a.position + (b.position - a.position) * t,
                                a.pressure + (b.pressure - a.pressure) * t});
            }
        }
        samples.append(smoothed[i]);
    }
    QVector<bool> keep(samples.size(), true);
    bool any = false;
    for (int i = 0; i < samples.size(); ++i) {
        const qreal ink = AnnotationRenderer::inkRadius(rect, item, samples[i].pressure);
        const QPointF d = samples[i].position - c;
        if (std::hypot(d.x(), d.y()) <= radius + ink) {
            keep[i] = false;
            any = true;
        }
    }
    if (!any) return result;
    result.changed = true;

    QVector<StrokePoint> run;
    auto flush = [&] {
        if (!run.isEmpty()) result.pieces.append(pieceFrom(item, run));
        run.clear();
    };
    for (int i = 0; i < samples.size(); ++i) {
        if (!keep[i]) {
            flush();
            continue;
        }
        StrokePoint p;
        p.x = float(std::clamp(samples[i].position.x() / imageSize.width(), 0.0, 1.0));
        p.y = float(std::clamp(samples[i].position.y() / imageSize.height(), 0.0, 1.0));
        p.pressure = float(std::clamp(samples[i].pressure, 0.0, 1.0));
        run.append(p);
    }
    flush();
    return result;
}

} // namespace atk::annotation
