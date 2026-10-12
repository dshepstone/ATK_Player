#pragma once

#include <QFrame>

class QLabel;
class QSlider;
class QSpinBox;

namespace atk::ui {

/// Floating size and opacity control shown beside the picture while drawing:
/// a vertical size slider with its value in pixels on top, and a vertical
/// opacity slider with its percentage below, as in SyncSketch.
///
/// It holds no settings of its own. AnnotationToolbar pushes the current
/// values in with setValues() and applies what the pill reports. MainWindow
/// owns its placement over the viewer area; the viewer itself never knows it
/// exists.
class BrushSizePill final : public QFrame {
    Q_OBJECT

public:
    explicit BrushSizePill(QWidget* parent = nullptr);

    /// Shows a size (source-image pixels) and opacity (0..1) without emitting.
    /// `eraser` relabels the size control, since it then sets the eraser size.
    void setValues(qreal sizePixels, qreal opacity, bool eraser);

    qreal sizePixels() const;
    qreal opacity() const;

signals:
    void sizeChanged(qreal sizePixels);
    void opacityChanged(qreal opacity);

private:
    QSpinBox* m_sizeField = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QSpinBox* m_opacityField = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_sizeCaption = nullptr;
    bool m_updating = false;
};

} // namespace atk::ui
