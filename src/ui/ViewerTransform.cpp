#include "ui/ViewerTransform.h"

#include <algorithm>
#include <cmath>

namespace atk::ui {
namespace {

bool validSize(const QSizeF& size)
{
    return size.width() > 0.0 && size.height() > 0.0
        && std::isfinite(size.width()) && std::isfinite(size.height());
}

} // namespace

void ViewerTransform::setDevicePixelRatio(qreal ratio)
{
    const qreal validRatio = std::isfinite(ratio) && ratio > 0.0 ? ratio : 1.0;
    if (qFuzzyCompare(m_devicePixelRatio, validRatio)) return;
    const qreal oldZoom = zoomRatio();
    m_devicePixelRatio = validRatio;
    if (isFit()) fit();
    else setScaleAt(oldZoom / m_devicePixelRatio,
                    QPointF(m_viewportSize.width() / 2.0, m_viewportSize.height() / 2.0));
}

void ViewerTransform::setSourceSize(const QSizeF& size, bool resetToFit)
{
    const QSizeF valid = validSize(size) ? size : QSizeF{};
    if (valid == m_sourceSize && !resetToFit) return;
    const QPointF focus = viewerToImage(
        QPointF(m_viewportSize.width() / 2.0, m_viewportSize.height() / 2.0));
    m_sourceSize = valid;
    if (resetToFit || isFit()) {
        fit();
    } else {
        const QPointF centre(m_viewportSize.width() / 2.0, m_viewportSize.height() / 2.0);
        const QPointF centredTopLeft((m_viewportSize.width() - m_sourceSize.width() * m_scale) / 2.0,
                                     (m_viewportSize.height() - m_sourceSize.height() * m_scale) / 2.0);
        m_pan = centre - focus * m_scale - centredTopLeft;
        clampPan();
    }
}

void ViewerTransform::setViewportSize(const QSizeF& size)
{
    const QSizeF valid = validSize(size) ? size : QSizeF{};
    if (valid == m_viewportSize) return;
    const QPointF oldCentre(m_viewportSize.width() / 2.0, m_viewportSize.height() / 2.0);
    const QPointF focus = viewerToImage(oldCentre);
    m_viewportSize = valid;
    if (isFit()) {
        fit();
    } else {
        const QPointF newCentre(m_viewportSize.width() / 2.0, m_viewportSize.height() / 2.0);
        const QPointF centredTopLeft((m_viewportSize.width() - m_sourceSize.width() * m_scale) / 2.0,
                                     (m_viewportSize.height() - m_sourceSize.height() * m_scale) / 2.0);
        m_pan = newCentre - focus * m_scale - centredTopLeft;
        clampPan();
    }
}

void ViewerTransform::fit()
{
    m_mode = Mode::Fit;
    m_scale = fitScale();
    m_pan = {};
}

void ViewerTransform::setActualSize()
{
    m_mode = Mode::Custom;
    m_scale = std::clamp<qreal>(1.0 / m_devicePixelRatio,
                                kMinimumZoom / m_devicePixelRatio,
                                kMaximumZoom / m_devicePixelRatio);
    m_pan = {};
    clampPan();
}

void ViewerTransform::zoomAt(qreal factor, const QPointF& viewerAnchor)
{
    if (!std::isfinite(factor) || factor <= 0.0) return;
    setScaleAt(m_scale * factor, viewerAnchor);
}

void ViewerTransform::panBy(const QPointF& delta)
{
    if (!std::isfinite(delta.x()) || !std::isfinite(delta.y())) return;
    m_mode = Mode::Custom;
    m_pan += delta;
    clampPan();
}

QRectF ViewerTransform::imageRect() const
{
    if (!validSize(m_sourceSize) || !validSize(m_viewportSize)) return {};
    const QSizeF scaled(m_sourceSize.width() * m_scale, m_sourceSize.height() * m_scale);
    const QPointF centred((m_viewportSize.width() - scaled.width()) / 2.0,
                          (m_viewportSize.height() - scaled.height()) / 2.0);
    return {centred + m_pan, scaled};
}

QPointF ViewerTransform::viewerToImage(const QPointF& point) const
{
    const QRectF target = imageRect();
    if (target.isEmpty() || m_scale <= 0.0) return {};
    return (point - target.topLeft()) / m_scale;
}

QPointF ViewerTransform::imageToViewer(const QPointF& point) const
{
    return imageRect().topLeft() + point * m_scale;
}

qreal ViewerTransform::fitScale() const
{
    if (!validSize(m_sourceSize) || !validSize(m_viewportSize)) return 1.0;
    return std::min(m_viewportSize.width() / m_sourceSize.width(),
                    m_viewportSize.height() / m_sourceSize.height());
}

void ViewerTransform::setScaleAt(qreal scale, const QPointF& viewerAnchor)
{
    if (!std::isfinite(scale)) return;
    const QPointF imageAnchor = viewerToImage(viewerAnchor);
    m_mode = Mode::Custom;
    const qreal minScale = kMinimumZoom / m_devicePixelRatio;
    const qreal maxScale = kMaximumZoom / m_devicePixelRatio;
    m_scale = std::clamp(scale, minScale, maxScale);
    const QPointF centredTopLeft((m_viewportSize.width() - m_sourceSize.width() * m_scale) / 2.0,
                                 (m_viewportSize.height() - m_sourceSize.height() * m_scale) / 2.0);
    m_pan = viewerAnchor - imageAnchor * m_scale - centredTopLeft;
    clampPan();
}

void ViewerTransform::clampPan()
{
    if (!validSize(m_sourceSize) || !validSize(m_viewportSize)) {
        m_pan = {};
        return;
    }
    const QSizeF scaled(m_sourceSize.width() * m_scale, m_sourceSize.height() * m_scale);
    if (scaled.width() <= m_viewportSize.width()) {
        m_pan.setX(0.0);
    } else {
        const qreal extent = (scaled.width() - m_viewportSize.width()) / 2.0;
        m_pan.setX(std::clamp(m_pan.x(), -extent, extent));
    }
    if (scaled.height() <= m_viewportSize.height()) {
        m_pan.setY(0.0);
    } else {
        const qreal extent = (scaled.height() - m_viewportSize.height()) / 2.0;
        m_pan.setY(std::clamp(m_pan.y(), -extent, extent));
    }
}

} // namespace atk::ui
