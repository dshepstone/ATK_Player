#include "ui/ComparisonCompositeWidget.h"
#include "playback/ComparisonCompositor.h"
#include "ui/Theme.h"
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace atk::ui {

ComparisonCompositeWidget::ComparisonCompositeWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ComparisonCompositeWidget"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void ComparisonCompositeWidget::setFrameA(const media::VideoFrame& frame) { m_frameA = frame; rebuild(); }
void ComparisonCompositeWidget::setFrameB(const media::VideoFrame& frame) { m_frameB = frame; rebuild(); }
void ComparisonCompositeWidget::setMode(playback::CompareLayout mode) { if (m_mode == mode) return; m_mode = mode; rebuild(); }
void ComparisonCompositeWidget::setWipePosition(int value) { value = std::clamp(value, 0, 100); if (m_wipePosition == value) return; m_wipePosition = value; if (m_mode == playback::CompareLayout::Wipe) rebuild(); }
void ComparisonCompositeWidget::setBlendAmount(int value) { value = std::clamp(value, 0, 100); if (m_blendAmount == value) return; m_blendAmount = value; if (m_mode == playback::CompareLayout::Blend) rebuild(); }
void ComparisonCompositeWidget::fitImage() { m_transform.fit(); emit zoomChanged(m_transform.zoomRatio() * 100.0, true); update(); }
void ComparisonCompositeWidget::showActualSize() { m_transform.setActualSize(); emit zoomChanged(m_transform.zoomRatio() * 100.0, false); update(); }
void ComparisonCompositeWidget::zoomIn() { m_transform.zoomAt(ViewerTransform::kWheelStepFactor, rect().center()); emit zoomChanged(m_transform.zoomRatio() * 100.0, m_transform.isFit()); update(); }
void ComparisonCompositeWidget::zoomOut() { m_transform.zoomAt(1.0 / ViewerTransform::kWheelStepFactor, rect().center()); emit zoomChanged(m_transform.zoomRatio() * 100.0, m_transform.isFit()); update(); }
void ComparisonCompositeWidget::restoreTransform(const ViewerTransform& value) { m_transform = value; m_transform.setDevicePixelRatio(devicePixelRatioF()); m_transform.setViewportSize(size()); update(); }
void ComparisonCompositeWidget::setVideoOnlyPresentation(bool value) { m_videoOnlyPresentation = value; update(); }

QImage ComparisonCompositeWidget::compositeImages(const QImage& a, const QImage& b,
                                                   playback::CompareLayout mode, int amount)
{
    return playback::ComparisonCompositor::compose(a, b, mode, amount);
}

void ComparisonCompositeWidget::rebuild()
{
    if (!m_frameA.isValid() && !m_frameB.isValid()) { m_composite = {}; update(); return; }
    const int amount = m_mode == playback::CompareLayout::Blend ? m_blendAmount : m_wipePosition;
    m_composite = compositeImages(m_frameA.image, m_frameB.image, m_mode, amount);
    syncTransform(m_transform.sourceSize().isEmpty());
    update();
}

void ComparisonCompositeWidget::syncTransform(bool reset)
{
    m_transform.setDevicePixelRatio(devicePixelRatioF());
    m_transform.setViewportSize(size());
    m_transform.setSourceSize(m_composite.size(), reset);
}

void ComparisonCompositeWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_videoOnlyPresentation ? Qt::black : theme::viewerBackground());
    if (m_composite.isNull()) return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_transform.zoomRatio() <= 1.0);
    painter.drawImage(m_transform.imageRect(), m_composite);
    if (m_mode == playback::CompareLayout::Wipe) {
        const QRectF image = m_transform.imageRect();
        const qreal x = image.left() + image.width() * m_wipePosition / 100.0;
        painter.setPen(QPen(QColor(255, 255, 255, 170), 1));
        painter.drawLine(QPointF(x, image.top()), QPointF(x, image.bottom()));
    }
}

void ComparisonCompositeWidget::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); syncTransform(); }
void ComparisonCompositeWidget::wheelEvent(QWheelEvent* event) { const qreal steps = event->angleDelta().y() / 120.0; if (!qFuzzyIsNull(steps)) { m_transform.zoomAt(std::pow(ViewerTransform::kWheelStepFactor, steps), event->position()); update(); event->accept(); } }
void ComparisonCompositeWidget::mousePressEvent(QMouseEvent* event) { emit activated(); if (event->button() == Qt::MiddleButton) { m_middlePanning = true; m_lastPanPosition = event->position(); setCursor(Qt::ClosedHandCursor); event->accept(); return; } if (event->button() == Qt::LeftButton && m_mode == playback::CompareLayout::Wipe) { m_draggingWipe = true; updateWipeFromPosition(event->position().x()); event->accept(); } }
void ComparisonCompositeWidget::mouseMoveEvent(QMouseEvent* event) { if (m_middlePanning) { m_transform.panBy(event->position() - m_lastPanPosition); m_lastPanPosition = event->position(); update(); event->accept(); } else if (m_draggingWipe) { updateWipeFromPosition(event->position().x()); event->accept(); } }
void ComparisonCompositeWidget::mouseReleaseEvent(QMouseEvent* event) { if (event->button() == Qt::MiddleButton && m_middlePanning) { m_middlePanning = false; unsetCursor(); event->accept(); } if (event->button() == Qt::LeftButton && m_draggingWipe) { m_draggingWipe = false; event->accept(); } }
void ComparisonCompositeWidget::mouseDoubleClickEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton) { fitImage(); event->accept(); } }
void ComparisonCompositeWidget::updateWipeFromPosition(qreal x) { const QRectF image = m_transform.imageRect(); if (image.width() <= 0) return; const int value = std::clamp(qRound((x - image.left()) * 100.0 / image.width()), 0, 100); setWipePosition(value); emit wipePositionChanged(value); }

} // namespace atk::ui
