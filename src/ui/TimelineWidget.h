#pragma once

#include "annotation/AnnotationHold.h"
#include "media/MediaMetadata.h"

#include <QElapsedTimer>
#include <QPair>
#include <QVector>
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
    /// Seconds between the time labels above the waveform: the smallest of
    /// 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15, 30 min, 1 h (then doubling)
    /// that keeps labels at least 80 px apart.
    static int64_t timeRulerStepSeconds(double pixelsPerSecond);

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
    void setWaveformTimeOffsetUs(int64_t offsetUs) { m_waveformTimeOffsetUs = offsetUs; refreshWaveform(); }
    int64_t waveformTimeOffsetUs() const { return m_waveformTimeOffsetUs; }

    /// Repaints the waveform band after new peaks arrive.
    void refreshWaveform();
    void zoomIn();
    void zoomOut();
    void fitEntire();
    void setBookmarkSnapEnabled(bool enabled) { m_bookmarkSnapEnabled = enabled; }
    bool isBookmarkSnapEnabled() const { return m_bookmarkSnapEnabled; }
    int positionForFrame(int64_t frame) const { return xForFrame(frame); }
    int64_t frameAtPosition(int x) const { return frameForX(x); }
    QRect rangeBookmarkRect(quint64 id) const;
    QRect rangeBookmarkStartCapRect(quint64 id) const;
    QRect rangeBookmarkEndCapRect(quint64 id) const;
    bool rangeBookmarkShowsLabel(quint64 id) const;
    quint64 bookmarkAtPosition(const QPoint& point) const;
    int64_t displayedFrame() const { return displayFrame(); }
    void setSelectedBookmark(quint64 id) { m_selectedBookmarkId = id; update(); }
    /// Frames that carry drawings, marked under the track so a reviewer can
    /// see where notes are and step to them.
    void setAnnotatedFrames(QVector<int64_t> frames);
    const QVector<int64_t>& annotatedFrames() const { return m_annotatedFrames; }
    /// Held drawings (see annotation::HoldMode), drawn as a faint bar under
    /// the track from each drawn frame through the frames it holds over.
    /// Empty when holding is off.
    void setHoldSpans(QVector<annotation::HoldSpan> spans);
    const QVector<annotation::HoldSpan>& holdSpans() const { return m_holdSpans; }
    /// Whether drawing markers can be dragged to another frame (off by
    /// default). Either way a click on a marker goes to its frame.
    void setMarkerDragEnabled(bool enabled);
    bool isMarkerDragEnabled() const { return m_markerDragEnabled; }
    /// The annotated frame whose marker (the wedge under the track) is at
    /// `point`, or -1. Dragging a marker moves that frame's notes and drawings.
    int64_t annotatedFrameAtPosition(const QPoint& point) const;
    /// Where a marker drag would drop, or -1 when no marker is being dragged.
    int64_t markerDropFrame() const { return m_markerDragFrom >= 0 ? m_markerDragTo : -1; }
    quint64 selectedBookmarkId() const { return m_selectedBookmarkId; }

    /// Several drawing markers picked at once, like keys in Maya's time
    /// slider: Shift+drag across the timeline selects every marker in that
    /// range (shown as a translucent blue band), Shift+click on a marker adds
    /// or removes it. Dragging inside the band then moves the whole group by
    /// the same number of frames, each frame keeping its own notes; Ctrl at
    /// release copies. A click outside the band, or Esc, clears it.
    const QVector<int64_t>& selectedMarkerFrames() const { return m_selectedMarkers; }
    /// The frames the blue band covers, or {-1, -1} when nothing is selected.
    QPair<int64_t, int64_t> markerSelectionRange() const;
    void setSelectedMarkerFrames(QVector<int64_t> frames);
    void clearMarkerSelection();
    /// Frames the selected group would move by while it is being dragged;
    /// 0 when no group drag is in progress.
    int64_t groupDragOffset() const { return m_groupDragActive ? m_groupDragOffset : 0; }
    /// Badge above the playhead showing the frame under the pointer while a
    /// scrub drag is in progress. Empty when not scrubbing.
    QRect scrubFrameLabelRect() const;
    QString scrubFrameLabelText() const;
    /// Drops only the pointer-preview overlay. The next painted position still
    /// comes from TimelineModel's authoritative presented frame.
    void followAuthoritativeFrame() { m_scrubbing = false; m_scrubFrame = -1; update(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /// A drag has distinct start, preview, and exact-release phases so the
    /// controller can coalesce expensive preview decodes without losing the
    /// final requested frame.
    void scrubStarted();
    void scrubPreviewRequested(qint64 frame);
    void scrubFinished(qint64 frame);
    void bookmarkSelected(quint64 id);
    /// A point marker or range band was double-clicked.
    void bookmarkActivated(quint64 id);
    /// A drawing marker was dragged from `fromFrame` and let go over `toFrame`
    /// (zero-based). `copy` is true when Ctrl was held at release: copy the
    /// frame's notes and drawings rather than move them.
    void annotatedFrameDropped(qint64 fromFrame, qint64 toFrame, bool copy);
    /// A group of selected drawing markers was dragged by `offset` frames.
    /// `copy` is true when Ctrl was held at release.
    void annotatedFramesDropped(const QVector<qint64>& frames, qint64 offset, bool copy);
    /// The marker selection changed (count of selected frames).
    void markerSelectionChanged(int count);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

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
    void paintBookmarks(QPainter& painter);
    void paintAnnotatedFrames(QPainter& painter);
    void paintHoldSpans(QPainter& painter);
    void paintPlayhead(QPainter& painter);
    void paintFrameLabels(QPainter& painter);
    void paintScrubFrameLabel(QPainter& painter);
    void paintMarkerDrag(QPainter& painter);
    void paintMarkerSelection(QPainter& painter);
    void paintGroupDrag(QPainter& painter);
    /// The band's pixel extent over the track and marker row, empty when
    /// nothing is selected or it is scrolled out of view.
    QRect markerSelectionRect() const;
    /// The selection's group-drag offset clamped so no frame leaves the clip.
    int64_t clampGroupOffset(int64_t offset) const;

    /// Emits a seek request, but no more often than the throttle interval
    /// while a drag is in progress.
    void requestSeek(int64_t frame, bool force);

    /// Frame the playhead should be drawn at: the scrub position while
    /// dragging, the model's current frame otherwise.
    int64_t displayFrame() const;
    int64_t snapFrame(int64_t frame, int x) const;

    timeline::TimelineModel* m_model = nullptr;
    const media::WaveformData* m_waveform = nullptr;
    int64_t m_mediaDurationUs = -1;
    int64_t m_waveformTimeOffsetUs = 0;
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
    bool m_bookmarkSnapEnabled = true;
    quint64 m_selectedBookmarkId = 0;
    QVector<int64_t> m_annotatedFrames;
    QVector<annotation::HoldSpan> m_holdSpans;
    bool m_markerDragEnabled = false;
    // Drawing-marker drag: the frame picked up and where it would land; -1
    // when no marker is held. m_markerDragCopy follows Ctrl during the drag.
    int64_t m_markerDragFrom = -1;
    int64_t m_markerDragTo = -1;
    bool m_markerDragCopy = false;

    // Multi-marker selection (sorted) and the band it shows; the band may be
    // wider than the markers when it came from a Shift+drag.
    QVector<int64_t> m_selectedMarkers;
    int64_t m_selectionFirst = -1;
    int64_t m_selectionLast = -1;
    // Shift+drag in progress: where it started and where the pointer is.
    bool m_rubberBanding = false;
    int64_t m_rubberFrom = -1;
    int64_t m_rubberTo = -1;
    // Press inside the band: becomes a group drag once the pointer moves a
    // few pixels, otherwise a click that seeks and clears the selection.
    bool m_groupPressed = false;
    bool m_groupDragActive = false;
    int m_groupPressX = 0;
    int64_t m_groupPressFrame = -1;
    int64_t m_groupDragOffset = 0;
    bool m_groupDragCopy = false;
};

} // namespace atk::ui
