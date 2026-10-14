#include "annotation/AnnotationTransform.h"

#include "annotation/AnnotationRenderer.h"

#include <QRectF>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace atk::annotation {
namespace {

constexpr qreal kEpsilon = 1e-6;

QPointF rotated(const QPointF& p, qreal degrees)
{
    const qreal r = qDegreesToRadians(degrees);
    const qreal c = std::cos(r);
    const qreal s = std::sin(r);
    return {p.x() * c - p.y() * s, p.x() * s + p.y() * c};
}

/// Into (-180, 180], so stored angles stay small and comparable.
qreal normalizedAngle(qreal degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180.0) degrees -= 360.0;
    if (degrees <= -180.0) degrees += 360.0;
    return degrees;
}

QPointF toPicture(const QSizeF& imageSize, const StrokePoint& p)
{
    return {qreal(p.x) * imageSize.width(), qreal(p.y) * imageSize.height()};
}

void setFromPicture(StrokePoint* p, const QSizeF& imageSize, const QPointF& picture)
{
    p->x = float(std::clamp(picture.x() / imageSize.width(), 0.0, 1.0));
    p->y = float(std::clamp(picture.y() / imageSize.height(), 0.0, 1.0));
}

bool isRotatableShape(AnnotationTool tool)
{
    return tool == AnnotationTool::Rectangle || tool == AnnotationTool::Ellipse;
}

} // namespace

QPointF TransformBox::toPicture(const QPointF& local) const { return center + rotated(local, angle); }

QPointF TransformBox::toLocal(const QPointF& picture) const { return rotated(picture - center, -angle); }

QPolygonF TransformBox::polygon() const
{
    const qreal w = size.width() / 2.0;
    const qreal h = size.height() / 2.0;
    return QPolygonF{toPicture({-w, -h}), toPicture({w, -h}), toPicture({w, h}), toPicture({-w, h})};
}

QPointF TransformBox::handlePoint(int handle) const
{
    const qreal w = size.width() / 2.0;
    const qreal h = size.height() / 2.0;
    static constexpr qreal kX[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    static constexpr qreal kY[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    if (handle < 0 || handle > 7) return center;
    return toPicture({kX[handle] * w, kY[handle] * h});
}

bool hasBoxHandles(const AnnotationItem& item)
{
    if (isFreehandTool(item.tool)) return item.points.size() > 1;
    return isRotatableShape(item.tool) || item.tool == AnnotationTool::Text;
}

bool hasEdgeHandles(const AnnotationItem& item)
{
    return hasBoxHandles(item) && item.tool != AnnotationTool::Text;
}

bool canRotate(const AnnotationItem& item)
{
    return hasBoxHandles(item) && item.tool != AnnotationTool::Text;
}

bool hasEndPointHandles(const AnnotationItem& item)
{
    return (item.tool == AnnotationTool::Line || item.tool == AnnotationTool::Arrow) && item.points.size() == 2;
}

TransformBox transformBox(const AnnotationItem& item, const QSizeF& imageSize)
{
    TransformBox box;
    if (item.points.isEmpty() || imageSize.isEmpty()) return box;
    const QRectF picture(QPointF(0.0, 0.0), imageSize);
    if (item.tool == AnnotationTool::Text) {
        const QRectF bounds = AnnotationRenderer::layoutText(picture, item).bounds;
        box.center = bounds.center();
        box.size = bounds.size();
        return box;
    }
    qreal minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const StrokePoint& p : item.points) {
        const QPointF q = toPicture(imageSize, p);
        minX = std::min(minX, q.x());
        minY = std::min(minY, q.y());
        maxX = std::max(maxX, q.x());
        maxY = std::max(maxY, q.y());
    }
    QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
    if (isFreehandTool(item.tool)) {
        const qreal pad = AnnotationRenderer::baseRadius(picture, item);
        bounds.adjust(-pad, -pad, pad, pad);
    }
    box.center = bounds.center();
    box.size = bounds.size();
    if (isRotatableShape(item.tool)) box.angle = item.rotation;
    return box;
}

TransformBox draggedBox(const TransformBox& start, int handle, const QPointF& press, const QPointF& current,
                        const TransformOptions& options)
{
    if (handle == kRotateHandle) {
        const QPointF a = press - start.center;
        const QPointF b = current - start.center;
        if (std::hypot(a.x(), a.y()) < kEpsilon || std::hypot(b.x(), b.y()) < kEpsilon) return start;
        qreal angle = start.angle
            + qRadiansToDegrees(std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x()));
        if (options.snapAngle) angle = std::round(angle / 15.0) * 15.0;
        TransformBox result = start;
        result.angle = normalizedAngle(angle);
        return result;
    }
    if (handle < 0 || handle > 7) return start;

    // Work in the box's own frame, so a rotated box scales along its sides.
    const QPointF delta = start.toLocal(current) - start.toLocal(press);
    const qreal w = start.size.width();
    const qreal h = start.size.height();
    qreal left = -w / 2.0, right = w / 2.0, top = -h / 2.0, bottom = h / 2.0;
    const bool ownsLeft = handle == 0 || handle == 6 || handle == 7;
    const bool ownsRight = handle == 2 || handle == 3 || handle == 4;
    const bool ownsTop = handle == 0 || handle == 1 || handle == 2;
    const bool ownsBottom = handle == 4 || handle == 5 || handle == 6;
    if (ownsLeft) {
        left += delta.x();
        if (options.fromCenter) right -= delta.x();
    }
    if (ownsRight) {
        right += delta.x();
        if (options.fromCenter) left -= delta.x();
    }
    if (ownsTop) {
        top += delta.y();
        if (options.fromCenter) bottom -= delta.y();
    }
    if (ownsBottom) {
        bottom += delta.y();
        if (options.fromCenter) top -= delta.y();
    }

    // Never thinner than the minimum (or than the box already was), and never
    // turned inside out.
    const qreal minW = std::min(options.minimumSize, w);
    const qreal minH = std::min(options.minimumSize, h);
    auto fit = [&](qreal& lo, qreal& hi, bool ownsLo, bool ownsHi, qreal length) {
        if (hi - lo >= length) return;
        if (options.fromCenter) {
            const qreal mid = (lo + hi) / 2.0;
            lo = mid - length / 2.0;
            hi = mid + length / 2.0;
        } else if (ownsLo) {
            lo = hi - length;
        } else if (ownsHi) {
            hi = lo + length;
        }
    };

    const bool corner = handle % 2 == 0;
    if (options.keepAspect && corner && w > kEpsilon && h > kEpsilon) {
        // The side that changed more leads, growing or shrinking; the other
        // follows in proportion.
        const qreal sx = (right - left) / w;
        const qreal sy = (bottom - top) / h;
        qreal scale = std::abs(sx - 1.0) >= std::abs(sy - 1.0) ? sx : sy;
        scale = std::max({scale, minW / w, minH / h});
        const qreal newW = w * scale;
        const qreal newH = h * scale;
        if (options.fromCenter) {
            left = -newW / 2.0;
            right = newW / 2.0;
            top = -newH / 2.0;
            bottom = newH / 2.0;
        } else {
            if (ownsLeft) left = right - newW;
            else right = left + newW;
            if (ownsTop) top = bottom - newH;
            else bottom = top + newH;
        }
    } else {
        fit(left, right, ownsLeft, ownsRight, minW);
        fit(top, bottom, ownsTop, ownsBottom, minH);
    }

    TransformBox result = start;
    result.center = start.toPicture(QPointF((left + right) / 2.0, (top + bottom) / 2.0));
    result.size = QSizeF(right - left, bottom - top);
    return result;
}

AnnotationItem transformedItem(const AnnotationItem& item, const QSizeF& imageSize, const TransformBox& from,
                               const TransformBox& to)
{
    if (item.points.isEmpty() || imageSize.isEmpty()) return item;
    const qreal sx = from.size.width() > kEpsilon ? to.size.width() / from.size.width() : 1.0;
    const qreal sy = from.size.height() > kEpsilon ? to.size.height() / from.size.height() : 1.0;
    auto map = [&](const QPointF& p) {
        const QPointF local = from.toLocal(p);
        return to.toPicture(QPointF(local.x() * sx, local.y() * sy));
    };

    AnnotationItem result = item;
    if (isRotatableShape(item.tool)) {
        // The outline stays a true rectangle or ellipse: its centre and size
        // follow the box, and the turn is kept as the item's rotation.
        const TransformBox own = transformBox(item, imageSize);
        const QPointF center = map(own.center);
        const QPointF half(own.size.width() * sx / 2.0, own.size.height() * sy / 2.0);
        setFromPicture(&result.points[0], imageSize, center - half);
        setFromPicture(&result.points[1], imageSize, center + half);
        result.rotation = normalizedAngle(item.rotation + to.angle - from.angle);
        if (qFuzzyIsNull(result.rotation)) result.rotation = 0.0;
        return result;
    }
    if (item.tool == AnnotationTool::Text) {
        // Type scales with the box; text keeps its proportions.
        result.size = std::clamp(item.size * sx, AnnotationLimits::kMinSize, AnnotationLimits::kMaxSize);
    }
    // Strokes, lines and arrows simply carry their points; any rotation is
    // baked in, so nothing else needs to know about it.
    for (StrokePoint& p : result.points) setFromPicture(&p, imageSize, map(toPicture(imageSize, p)));
    return result;
}

AnnotationItem movedEndPoint(const AnnotationItem& item, int handle, const QPointF& normalized)
{
    if (!hasEndPointHandles(item) || (handle != kStartPointHandle && handle != kEndPointHandle)) return item;
    AnnotationItem result = item;
    StrokePoint& p = result.points[handle == kStartPointHandle ? 0 : 1];
    p.x = float(std::clamp(normalized.x(), 0.0, 1.0));
    p.y = float(std::clamp(normalized.y(), 0.0, 1.0));
    return result;
}

} // namespace atk::annotation
