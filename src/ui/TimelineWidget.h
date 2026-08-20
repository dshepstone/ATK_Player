#pragma once

#include <QWidget>

#include <cstdint>

namespace atk::timeline { class TimelineModel; }

namespace atk::ui {

/// Frame ruler and scrubber.
///
/// Renders a TimelineModel: the track, the in/out range, bookmark markers and
/// the playhead, with the first and last frame numbers at either end. Dragging
/// requests a seek; the widget never moves the playhead itself, so the model
/// stays the single source of truth and an API-driven seek looks identical to a
/// mouse-driven one.
///
/// PHASE 0 STATUS: drawing and scrubbing are real. With no media loaded the
/// track shows the 0 / 0 empty state.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);
    ~TimelineWidget() override;

    /// The model is observed, not owned, and must outlive the widget.
    void setModel(timeline::TimelineModel* model);
    timeline::TimelineModel* model() const { return m_model; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /// The user asked to move the playhead. The receiver decides whether to
    /// honour it -- the widget does not assume the seek succeeded.
    void seekRequested(qint64 frame);
    /// A bookmark marker was double-clicked.
    void bookmarkActivated(qint64 frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    /// The horizontal strip the track occupies, inset for the frame labels.
    QRect trackRect() const;
    /// Maps a frame to an x coordinate inside trackRect(), and back.
    int xForFrame(int64_t frame) const;
    int64_t frameForX(int x) const;
    /// Last frame index, or 0 when nothing is loaded.
    int64_t lastFrame() const;

    void paintTrack(QPainter& painter);
    void paintRange(QPainter& painter);
    void paintBookmarks(QPainter& painter);
    void paintPlayhead(QPainter& painter);
    void paintFrameLabels(QPainter& painter);

    timeline::TimelineModel* m_model = nullptr;
    bool m_scrubbing = false;
};

} // namespace atk::ui
