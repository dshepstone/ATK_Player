#pragma once

#include <QWidget>

#include <cstdint>

namespace atk::timeline { class TimelineModel; }

namespace atk::ui {

/// Maya-style editor for the visible timeline viewport. The full groove is the
/// source extent; the selected body is TimelineViewport and owns no duplicate
/// state.
class TimelineRangeSlider : public QWidget {
    Q_OBJECT
public:
    explicit TimelineRangeSlider(QWidget* parent = nullptr);
    void setModel(timeline::TimelineModel* model);
    timeline::TimelineModel* model() const { return m_model; }

    int positionForSourceFrame(int64_t frame) const;
    int64_t sourceFrameAtPosition(int x) const;
    QRect selectionRect() const;
    QSize sizeHint() const override { return {800, 24}; }
    QSize minimumSizeHint() const override { return {180, 24}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    enum class DragMode { None, Left, Body, Right };
    QRect grooveRect() const;

    timeline::TimelineModel* m_model = nullptr;
    DragMode m_dragMode = DragMode::None;
    int m_pressX = 0;
    int64_t m_pressStart = 0;
    int64_t m_pressEnd = 0;
};

} // namespace atk::ui
