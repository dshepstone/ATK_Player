#include "annotation/AnnotationRenderer.h"

#include "annotation/BrushTip.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTextLayout>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace atk::annotation {
namespace {

QPointF toPainter(const QRectF& rect, const StrokePoint& p)
{
    return {rect.left() + qreal(p.x) * rect.width(), rect.top() + qreal(p.y) * rect.height()};
}

/// How each freehand tool turns pressure into width and opacity.
struct BrushShape {
    qreal minWidth = 1.0;    // radius factor at zero pressure
    qreal minOpacity = 1.0;  // alpha factor at zero pressure
    bool constantWidth = false;
    bool soft = false;
    bool grainy = false;
};

BrushShape brushShape(AnnotationTool tool)
{
    switch (tool) {
    case AnnotationTool::Pencil: return {0.35, 0.30, false, false, true};
    case AnnotationTool::Pen: return {0.12, 1.00, false, false, false};
    case AnnotationTool::Brush: return {0.25, 0.25, false, true, false};
    case AnnotationTool::Marker: return {1.00, 1.00, true, false, false};
    case AnnotationTool::Stamp: return {0.25, 0.35, false, false, false};
    default: return {};
    }
}

qreal mix(qreal a, qreal b, qreal t) { return a + (b - a) * t; }

/// Catmull-Rom point at t between p1 and p2.
QPointF catmullRom(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t)
{
    const qreal t2 = t * t;
    const qreal t3 = t2 * t;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                  + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

/// Convex hull of two circles as a fill path (round caps, round joins).
void addCapsule(QPainterPath& path, const QPointF& a, qreal ra, const QPointF& b, qreal rb)
{
    path.addEllipse(a, ra, ra);
    path.addEllipse(b, rb, rb);
    const QPointF d = b - a;
    const qreal length = std::hypot(d.x(), d.y());
    if (length <= std::abs(ra - rb) || length < 1e-6) return; // one circle contains the other
    const QPointF n(-d.y() / length, d.x() / length);
    QPolygonF quad;
    quad << a + n * ra << b + n * rb << b - n * rb << a - n * ra;
    path.addPolygon(quad);
    path.closeSubpath();
}

qreal distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    qreal t = len2 > 0.0 ? QPointF::dotProduct(p - a, ab) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF closest = a + ab * t;
    return std::hypot(p.x() - closest.x(), p.y() - closest.y());
}

QRectF textRect(const QRectF& rect, const AnnotationItem& item)
{
    return AnnotationRenderer::layoutText(rect, item).bounds;
}

void paintStamp(QPainter& painter, const QRectF& rect, const AnnotationItem& item, const BrushTip& tip);

void paintFreehand(QPainter& painter, const QRectF& rect, const AnnotationItem& item)
{
    if (item.tool == AnnotationTool::Stamp) {
        const BrushTip tip = BrushTipRegistry::instance().find(item.brushId);
        if (tip.isValid()) {
            paintStamp(painter, rect, item, tip);
            return;
        }
        // The tip is not available (a set removed and no copy in the
        // project): draw a soft round brush rather than nothing.
        AnnotationItem fallback = item;
        fallback.tool = AnnotationTool::Brush;
        fallback.brushId.clear();
        paintFreehand(painter, rect, fallback);
        return;
    }
    const BrushShape shape = brushShape(item.tool);
    const QVector<AnnotationRenderer::PathSample> path = AnnotationRenderer::smoothedPath(rect, item);
    if (path.isEmpty()) return;
    // Layer covering the stroke, in device pixels so strokes stay crisp on
    // high-DPI displays.
    QRectF bounds = AnnotationRenderer::itemBounds(rect, item).adjusted(-2, -2, 2, 2);
    qreal dpr = 1.0;
    if (QPaintDevice* device = painter.device()) {
        dpr = std::max<qreal>(1.0, device->devicePixelRatioF());
        // Only the visible part needs a layer: at high zoom a stroke can be far
        // larger than the screen.
        bool invertible = false;
        const QTransform inverse = painter.combinedTransform().inverted(&invertible);
        if (invertible) {
            const QRectF deviceRect(0, 0, device->width(), device->height());
            bounds = bounds.intersected(inverse.mapRect(deviceRect).adjusted(-2, -2, 2, 2));
        }
    }
    const QRect clipped = bounds.toAlignedRect();
    if (clipped.isEmpty()) return;
    const QSize layerSize(std::max(1, qCeil(clipped.width() * dpr)), std::max(1, qCeil(clipped.height() * dpr)));
    // Hard cap so a pathological transform can never allocate gigabytes.
    if (qint64(layerSize.width()) * layerSize.height() > 64LL * 1024 * 1024) return;

    QImage layer(layerSize, QImage::Format_ARGB32_Premultiplied);
    layer.setDevicePixelRatio(dpr);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHint(QPainter::Antialiasing, true);
        lp.translate(-clipped.topLeft());
        lp.setPen(Qt::NoPen);
        lp.setCompositionMode(QPainter::CompositionMode_Source);

        auto radiusAt = [&](qreal p) { return AnnotationRenderer::inkRadius(rect, item, p); };
        auto alphaAt = [&](qreal p) { return mix(shape.minOpacity, 1.0, p); };

        // Soft brush: concentric passes, outer faint to inner full, so the
        // edge falls off. Each pass overwrites (Source), so they never stack.
        const qreal rings[] = {1.0, 0.82, 0.62};
        const qreal ringAlpha[] = {0.30, 0.65, 1.0};
        const int passes = shape.soft ? 3 : 1;
        for (int pass = 0; pass < passes; ++pass) {
            const qreal ringScale = shape.soft ? rings[pass] : 1.0;
            const qreal ringOpacity = shape.soft ? ringAlpha[pass] : 1.0;
            if (path.size() == 1) {
                QColor c = item.color;
                c.setAlphaF(c.alphaF() * alphaAt(path[0].pressure) * ringOpacity);
                lp.setBrush(c);
                const qreal r = radiusAt(path[0].pressure) * ringScale;
                lp.drawEllipse(path[0].position, r, r);
                continue;
            }
            for (int i = 1; i < path.size(); ++i) {
                const auto& a = path[i - 1];
                const auto& b = path[i];
                QPainterPath capsule;
                capsule.setFillRule(Qt::WindingFill);
                addCapsule(capsule, a.position, radiusAt(a.pressure) * ringScale, b.position,
                           radiusAt(b.pressure) * ringScale);
                QColor c = item.color;
                c.setAlphaF(c.alphaF() * alphaAt(0.5 * (a.pressure + b.pressure)) * ringOpacity);
                lp.setBrush(c);
                lp.drawPath(capsule);
            }
        }
    }

    if (shape.grainy) {
        // Paper grain: knock alpha back per pixel, deterministic per seed.
        constexpr float kStrength = 0.55f;
        const int grainX = qRound(clipped.left() * dpr);
        const int grainY = qRound(clipped.top() * dpr);
        for (int y = 0; y < layer.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(layer.scanLine(y));
            for (int x = 0; x < layer.width(); ++x) {
                const QRgb px = line[x];
                if (qAlpha(px) == 0) continue;
                // Keyed to device position, not layer position, so a stroke
                // split by the eraser keeps exactly the grain it had.
                const float keep = 1.0f - kStrength * AnnotationRenderer::grain(x + grainX, y + grainY, item.textureSeed);
                line[x] = qRgba(int(qRed(px) * keep), int(qGreen(px) * keep), int(qBlue(px) * keep),
                                int(qAlpha(px) * keep));
            }
        }
    }

    painter.save();
    painter.setOpacity(painter.opacity() * item.opacity);
    painter.drawImage(QRectF(clipped), layer);
    painter.restore();
}

/// Repeats a brush tip along the stroke, like a Photoshop sampled brush:
/// dab size and opacity follow pressure, dabs are spaced by the tip's spacing
/// and build up within the stroke, and the whole stroke takes the item opacity.
void paintStamp(QPainter& painter, const QRectF& rect, const AnnotationItem& item, const BrushTip& tip)
{
    const QVector<AnnotationRenderer::PathSample> path = AnnotationRenderer::smoothedPath(rect, item);
    if (path.isEmpty()) return;
    QRectF bounds = AnnotationRenderer::itemBounds(rect, item).adjusted(-2, -2, 2, 2);
    qreal dpr = 1.0;
    if (QPaintDevice* device = painter.device()) {
        dpr = std::max<qreal>(1.0, device->devicePixelRatioF());
        bool invertible = false;
        const QTransform inverse = painter.combinedTransform().inverted(&invertible);
        if (invertible) bounds = bounds.intersected(inverse.mapRect(QRectF(0, 0, device->width(), device->height())).adjusted(-2, -2, 2, 2));
    }
    const QRect clipped = bounds.toAlignedRect();
    if (clipped.isEmpty()) return;
    const QSize layerSize(std::max(1, qCeil(clipped.width() * dpr)), std::max(1, qCeil(clipped.height() * dpr)));
    if (qint64(layerSize.width()) * layerSize.height() > 64LL * 1024 * 1024) return;

    // Tint the tip once, at no more resolution than the largest dab needs.
    const qreal maxDiameter = 2.0 * AnnotationRenderer::inkRadius(rect, item, 1.0);
    const int side = std::clamp(qCeil(maxDiameter * dpr), 2, 1024);
    const QImage scaledMask = tip.mask.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage dab(scaledMask.size(), QImage::Format_ARGB32_Premultiplied);
    const QColor c = item.color;
    for (int y = 0; y < dab.height(); ++y) {
        const uchar* m = scaledMask.constScanLine(y);
        auto* out = reinterpret_cast<QRgb*>(dab.scanLine(y));
        for (int x = 0; x < dab.width(); ++x) {
            const int a = m[x] * c.alpha() / 255;
            out[x] = qRgba(c.red() * a / 255, c.green() * a / 255, c.blue() * a / 255, a);
        }
    }
    const qreal aspect = qreal(scaledMask.width()) / std::max(1, scaledMask.height());

    QImage layer(layerSize, QImage::Format_ARGB32_Premultiplied);
    layer.setDevicePixelRatio(dpr);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHint(QPainter::SmoothPixmapTransform, true);
        lp.translate(-clipped.topLeft());
        auto stampAt = [&](const QPointF& at, qreal pressure) {
            const qreal diameter = 2.0 * AnnotationRenderer::inkRadius(rect, item, pressure);
            const QSizeF size = aspect >= 1.0 ? QSizeF(diameter, diameter / aspect) : QSizeF(diameter * aspect, diameter);
            lp.save();
            lp.translate(at);
            if (!qFuzzyIsNull(tip.angle)) lp.rotate(-tip.angle);
            lp.setOpacity(mix(0.35, 1.0, std::clamp(pressure, 0.0, 1.0)));
            lp.drawImage(QRectF(QPointF(-size.width() / 2.0, -size.height() / 2.0), size), dab);
            lp.restore();
        };
        // Walk the path, placing a dab every `spacing` diameters.
        stampAt(path[0].position, path[0].pressure);
        qreal carried = 0.0;
        for (int i = 1; i < path.size(); ++i) {
            const QPointF a = path[i - 1].position;
            const QPointF b = path[i].position;
            const qreal length = std::hypot(b.x() - a.x(), b.y() - a.y());
            qreal travelled = 0.0;
            for (;;) {
                const qreal t = travelled / std::max(length, 1e-9);
                const qreal pressure = mix(path[i - 1].pressure, path[i].pressure, std::clamp(t, 0.0, 1.0));
                const qreal step = std::max(0.5, tip.spacing * 2.0 * AnnotationRenderer::inkRadius(rect, item, pressure));
                if (travelled + (step - carried) > length) {
                    carried += length - travelled;
                    break;
                }
                travelled += step - carried;
                carried = 0.0;
                const qreal u = travelled / std::max(length, 1e-9);
                stampAt(a + (b - a) * u, mix(path[i - 1].pressure, path[i].pressure, u));
            }
        }
    }
    painter.save();
    painter.setOpacity(painter.opacity() * item.opacity);
    painter.drawImage(QRectF(clipped), layer);
    painter.restore();
}

void paintShape(QPainter& painter, const QRectF& rect, const AnnotationItem& item)
{
    const QPointF a = toPainter(rect, item.points[0]);
    const QPointF b = toPainter(rect, item.points[1]);
    const qreal width = 2.0 * AnnotationRenderer::baseRadius(rect, item);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(painter.opacity() * item.opacity);
    QPen pen(item.color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    switch (item.tool) {
    case AnnotationTool::Line:
        painter.drawLine(a, b);
        break;
    case AnnotationTool::Arrow: {
        QPainterPath arrow;
        arrow.moveTo(a);
        arrow.lineTo(b);
        const qreal angle = std::atan2(b.y() - a.y(), b.x() - a.x());
        const qreal head = std::max(width * 4.0, rect.height() * 0.02);
        for (qreal side : {-1.0, 1.0}) {
            const qreal theta = angle + M_PI + side * qDegreesToRadians(28.0);
            arrow.moveTo(b);
            arrow.lineTo(b + QPointF(std::cos(theta), std::sin(theta)) * head);
        }
        painter.drawPath(arrow);
        break;
    }
    case AnnotationTool::Rectangle:
        painter.drawRect(QRectF(a, b).normalized());
        break;
    case AnnotationTool::Ellipse:
        painter.drawEllipse(QRectF(a, b).normalized());
        break;
    default:
        break;
    }
    painter.restore();
}

void paintText(QPainter& painter, const QRectF& rect, const AnnotationItem& item)
{
    const AnnotationRenderer::TextBlock block = AnnotationRenderer::layoutText(rect, item);
    QPainterPath path;
    for (const AnnotationRenderer::TextLine& line : block.lines)
        if (!line.text.isEmpty()) path.addText(line.baseline, block.font, line.text);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(painter.opacity() * item.opacity);
    // A thin dark outline keeps text readable over any picture.
    const qreal outline = std::max<qreal>(1.0, block.font.pixelSize() / 10.0);
    painter.strokePath(path, QPen(QColor(0, 0, 0, 160), outline * 2, Qt::SolidLine, Qt::RoundCap,
                                  Qt::RoundJoin));
    painter.fillPath(path, item.color);
    painter.restore();
}

} // namespace

QFont AnnotationRenderer::textFont(const QRectF& imageRect, const AnnotationItem& item)
{
    QFont font;
    font.setPixelSize(std::max(6, qRound(item.size * 5.0 * imageRect.height())));
    font.setBold(true);
    return font;
}

AnnotationRenderer::TextBlock AnnotationRenderer::layoutText(const QRectF& imageRect, const AnnotationItem& item)
{
    TextBlock block;
    block.font = textFont(imageRect, item);
    if (item.points.isEmpty()) return block;
    const QPointF anchor = toPainter(imageRect, item.points.constFirst());
    const bool box = isBoxText(item);
    const qreal boxWidth = box ? std::max<qreal>(1.0, (item.points[1].x - item.points[0].x) * imageRect.width()) : 0.0;

    QString text = item.text;
    text.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(text, block.font);
    QTextOption option;
    option.setWrapMode(box ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    layout.setTextOption(option);
    layout.setCacheEnabled(true);
    layout.beginLayout();
    qreal y = 0.0;
    qreal widest = 0.0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(box ? boxWidth : 1.0e7);
        line.setPosition(QPointF(0.0, y));
        QString segment = text.mid(line.textStart(), line.textLength());
        while (!segment.isEmpty() && (segment.back() == QChar::LineSeparator || segment.back().isSpace()))
            segment.chop(1);
        block.lines.append({segment, anchor + QPointF(0.0, y + line.ascent()), line.naturalTextWidth()});
        widest = std::max(widest, line.naturalTextWidth());
        y += line.height();
    }
    layout.endLayout();
    // A box keeps the size it was drawn at, even before it is filled.
    const qreal boxHeight = box ? (item.points[1].y - item.points[0].y) * imageRect.height() : 0.0;
    block.bounds = QRectF(anchor, QSizeF(box ? boxWidth : widest, std::max(y, boxHeight)));
    return block;
}

qreal AnnotationRenderer::inkRadius(const QRectF& imageRect, const AnnotationItem& item, qreal pressure)
{
    const BrushShape shape = brushShape(item.tool);
    const qreal radius = baseRadius(imageRect, item);
    return shape.constantWidth ? radius : radius * mix(shape.minWidth, 1.0, std::clamp(pressure, 0.0, 1.0));
}

qreal AnnotationRenderer::baseRadius(const QRectF& imageRect, const AnnotationItem& item)
{
    return std::max<qreal>(0.5, 0.5 * item.size * imageRect.height());
}

QVector<AnnotationRenderer::PathSample> AnnotationRenderer::smoothedPath(const QRectF& imageRect,
                                                                         const AnnotationItem& item)
{
    QVector<PathSample> raw;
    raw.reserve(item.points.size());
    for (const StrokePoint& p : item.points) raw.append({toPainter(imageRect, p), qreal(p.pressure)});
    if (raw.size() < 3) return raw;

    QVector<PathSample> out;
    out.reserve(raw.size() * 2);
    out.append(raw.constFirst());
    for (int i = 0; i + 1 < raw.size(); ++i) {
        const QPointF p0 = raw[std::max(0, i - 1)].position;
        const QPointF p1 = raw[i].position;
        const QPointF p2 = raw[i + 1].position;
        const QPointF p3 = raw[std::min<int>(int(raw.size()) - 1, i + 2)].position;
        const qreal length = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        // About one sample per 2 px keeps curves round without bloating.
        const int steps = std::clamp(int(length / 2.0), 1, 16);
        for (int s = 1; s <= steps; ++s) {
            const qreal t = qreal(s) / steps;
            out.append({catmullRom(p0, p1, p2, p3, t), mix(raw[i].pressure, raw[i + 1].pressure, t)});
        }
    }
    return out;
}

QRectF AnnotationRenderer::itemBounds(const QRectF& imageRect, const AnnotationItem& item)
{
    if (item.points.isEmpty()) return {};
    if (item.tool == AnnotationTool::Text) return textRect(imageRect, item);
    qreal minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const StrokePoint& p : item.points) {
        const QPointF q = toPainter(imageRect, p);
        minX = std::min(minX, q.x());
        minY = std::min(minY, q.y());
        maxX = std::max(maxX, q.x());
        maxY = std::max(maxY, q.y());
    }
    qreal pad = baseRadius(imageRect, item);
    if (item.tool == AnnotationTool::Arrow)
        pad = std::max(pad * 8.0 + 2.0, imageRect.height() * 0.02 + pad);
    if (item.tool == AnnotationTool::Stamp) pad *= 1.42; // a rotated square tip
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).adjusted(-pad - 1, -pad - 1, pad + 1, pad + 1);
}

bool AnnotationRenderer::hitTest(const QRectF& imageRect, const AnnotationItem& item,
                                 const QPointF& position, qreal tolerance)
{
    if (item.points.isEmpty()) return false;
    const qreal reach = baseRadius(imageRect, item) + tolerance;
    if (!itemBounds(imageRect, item).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(position))
        return false;

    if (item.tool == AnnotationTool::Text)
        return textRect(imageRect, item).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(position);

    if (isFreehandTool(item.tool)) {
        if (item.points.size() == 1) {
            const QPointF c = toPainter(imageRect, item.points[0]);
            return std::hypot(position.x() - c.x(), position.y() - c.y()) <= reach;
        }
        for (int i = 1; i < item.points.size(); ++i) {
            if (distanceToSegment(position, toPainter(imageRect, item.points[i - 1]),
                                  toPainter(imageRect, item.points[i])) <= reach)
                return true;
        }
        return false;
    }

    const QPointF a = toPainter(imageRect, item.points[0]);
    const QPointF b = toPainter(imageRect, item.points[1]);
    if (item.tool == AnnotationTool::Line || item.tool == AnnotationTool::Arrow)
        return distanceToSegment(position, a, b) <= reach;

    QPainterPath outline;
    if (item.tool == AnnotationTool::Rectangle) outline.addRect(QRectF(a, b).normalized());
    else outline.addEllipse(QRectF(a, b).normalized());
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * reach);
    return stroker.createStroke(outline).contains(position);
}

float AnnotationRenderer::grain(int x, int y, quint32 seed)
{
    // Small integer hash (PCG-style mix); cheap, stable across platforms.
    quint32 h = quint32(x) * 0x8da6b343u ^ quint32(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}

void AnnotationRenderer::paintItem(QPainter& painter, const QRectF& imageRect, const AnnotationItem& item)
{
    if (!isValidItem(item) || imageRect.isEmpty()) return;
    if (isFreehandTool(item.tool)) paintFreehand(painter, imageRect, item);
    else if (isShapeTool(item.tool)) paintShape(painter, imageRect, item);
    else if (item.tool == AnnotationTool::Text) paintText(painter, imageRect, item);
}

void AnnotationRenderer::paintFrame(QPainter& painter, const QRectF& imageRect, const FrameAnnotation& frame)
{
    for (const AnnotationItem& item : frame.items) paintItem(painter, imageRect, item);
}

void AnnotationRenderer::renderOnto(QImage& image, const FrameAnnotation& frame)
{
    if (image.isNull() || frame.isEmpty()) return;
    QPainter painter(&image);
    const qreal dpr = image.devicePixelRatio();
    paintFrame(painter, QRectF(QPointF(0, 0), QSizeF(image.size()) / dpr), frame);
}

} // namespace atk::annotation
