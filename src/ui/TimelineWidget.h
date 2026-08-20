#pragma once

#include "media/MediaMetadata.h"

#include <QElapsedTimer>
#include <QWidget>

#include <cstdint>

namespace atk::media { class WaveformData; }
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

    /// Waveform to draw. Not owned; the controller keeps it alive and calls
    /// refreshWaveform() as analysis delivers more. Passing nullptr hides it.
    void setWaveform(const media::WaveformData* waveform);

    /// Media duration, needed to map waveform time onto the track. The waveform
    /// is indexed by media time while the track is indexed by frame, and those
    /// are only interchangeable through the real frame rate.
    void setMediaDuration(int64_t durationUs);

    /// Repaints the waveform band after new peaks arrive.
    void refreshWaveform();
    void zoomIn();
    void zoomOut();
    void fitEntire();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /// A drag has distinct start, preview, and exact-release phases so the
    /// controller can coalesce expensive preview decodes without losing the
    /// final requested frame.
    void scrubStarted();
    void scrubPreviewRequested(qint64 frame);
    void scrubFinished(qint64 frame);
    /// A bookmark marker was double-clicked.
    void bookmarkActivated(qint64 frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// The horizontal strip the track occupies, inset for the frame labels.
    QRect trackRect() const;
    QRect waveformRect() const;

    /// Media time at a horizontal position, for waveform lookup.
    int64_t mediaTimeForX(int x) const;
    /// Maps a frame to an x coordinate inside trackRect(), and back.
    int xForFrame(int64_t frame) const;
    int64_t frameForX(int x) const;
    /// Last frame index, or 0 when nothing is loaded.
    int64_t lastFrame() const;

    void paintWaveform(QPainter& painter);
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
    const media::WaveformData* m_waveform = nullptr;
    int64_t m_mediaDurationUs = -1;
    bool m_scrubbing = false;
    bool m_panning = false;
    int m_lastPanX = 0;
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
