#pragma once

#include "media/VideoFrame.h"
#include "playback/CompareSession.h"
#include "ui/ViewerTransform.h"
#include <QWidget>

namespace atk::ui {

class ComparisonCompositeWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ComparisonCompositeWidget(QWidget* parent = nullptr);
    void setFrameA(const media::VideoFrame& frame);
    void setFrameB(const media::VideoFrame& frame);
    void setMode(playback::CompareLayout mode);
    playback::CompareLayout mode() const { return m_mode; }
    void setWipePosition(int percent);
    void setBlendAmount(int percent);
    int wipePosition() const { return m_wipePosition; }
    int blendAmount() const { return m_blendAmount; }
    void fitImage();
    void showActualSize();
    void zoomIn();
    void zoomOut();
    void restoreTransform(const ViewerTransform& transform);
    const ViewerTransform& transform() const { return m_transform; }
    void setVideoOnlyPresentation(bool enabled);

    static QImage compositeImages(const QImage& a, const QImage& b,
                                  playback::CompareLayout mode, int amount);

signals:
    void wipePositionChanged(int percent);
    void activated();
    void zoomChanged(qreal percent, bool fitMode);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    void rebuild();
    void syncTransform(bool resetToFit = false);
    void updateWipeFromPosition(qreal x);
    media::VideoFrame m_frameA;
    media::VideoFrame m_frameB;
    QImage m_composite;
    ViewerTransform m_transform;
    playback::CompareLayout m_mode = playback::CompareLayout::Wipe;
    int m_wipePosition = 50;
    int m_blendAmount = 50;
    bool m_videoOnlyPresentation = false;
    bool m_draggingWipe = false;
    bool m_middlePanning = false;
    QPointF m_lastPanPosition;
};

} // namespace atk::ui
