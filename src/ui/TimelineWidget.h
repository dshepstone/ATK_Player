#pragma once

#include <QElapsedTimer>
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

    /// Emits a seek request, but no more often than the throttle interval
    /// while a drag is in progress.
    void requestSeek(int64_t frame, bool force);

    /// Frame the playhead should be drawn at: the scrub position while
    /// dragging, the model's current frame otherwise.
    int64_t displayFrame() const;

    timeline::TimelineModel* m_model = nullptr;
    bool m_scrubbing = false;
    /// Paces preview seeks during a drag so the decoder is not handed a new
    /// target on every mouse move; the exact seek is issued on release.
    QElapsedTimer m_scrubThrottle;
    int64_t m_lastRequestedFrame = -1;

    /// Where the pointer is during a drag, independent of what has decoded.
    ///
    /// The playhead used to be drawn straight from the model, which only moves
    /// when a decoded frame is presented -- so it could not advance faster than
    /// FFmpeg could seek, and the cursor visibly outran it. Tracking the
    /// requested position separately lets the playhead follow the mouse at
    /// pointer rate while the picture catches up behind it.
    ///
    /// -1 when not scrubbing.
    int64_t m_scrubFrame = -1;
};

} // namespace atk::ui
