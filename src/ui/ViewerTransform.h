#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace atk::ui {

/// Pure viewer-navigation state. Coordinates are QWidget logical pixels;
/// source coordinates are display-corrected image pixels.
class ViewerTransform {
public:
    enum class Mode { Fit, Custom };

    static constexpr qreal kMinimumZoom = 0.05;
    static constexpr qreal kMaximumZoom = 8.0;
    static constexpr qreal kWheelStepFactor = 1.1;

    Mode mode() const { return m_mode; }
    bool isFit() const { return m_mode == Mode::Fit; }
    qreal scale() const { return m_scale; }
    qreal zoomRatio() const { return m_scale * m_devicePixelRatio; }
    QPointF pan() const { return m_pan; }
    QSizeF sourceSize() const { return m_sourceSize; }
    QSizeF viewportSize() const { return m_viewportSize; }

    void setDevicePixelRatio(qreal ratio);
    void setSourceSize(const QSizeF& size, bool resetToFit = false);
    void setViewportSize(const QSizeF& size);

    void fit();
    void setActualSize();
    void zoomAt(qreal factor, const QPointF& viewerAnchor);
    void panBy(const QPointF& delta);

    QRectF imageRect() const;
    QPointF viewerToImage(const QPointF& point) const;
    QPointF imageToViewer(const QPointF& point) const;

private:
    qreal fitScale() const;
    void setScaleAt(qreal scale, const QPointF& viewerAnchor);
    void clampPan();

    Mode m_mode = Mode::Fit;
    qreal m_scale = 1.0;
    qreal m_devicePixelRatio = 1.0;
    QPointF m_pan;
    QSizeF m_sourceSize;
    QSizeF m_viewportSize;
};

} // namespace atk::ui
