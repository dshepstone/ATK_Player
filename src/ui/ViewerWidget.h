#pragma once

#include "annotation/AnnotationGhosting.h"
#include "annotation/AnnotationToolSettings.h"
#include "annotation/AnnotationTypes.h"
#include "media/VideoFrame.h"
#include "ui/ViewerTransform.h"

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <memory>
#include <optional>

class QTabletEvent;
class QTextEdit;

namespace atk::annotation { class StrokeBuilder; }

namespace atk::ui {

/// Displays one video frame.
///
/// Reusable and self-contained: it holds a frame and draws it, and knows
/// nothing about decoding, transport or the timeline. A second instance is what
/// viewer B becomes in A/B comparison (milestone M4), which is why it takes a
/// frame rather than reaching for a player.
///
/// PHASE 0 STATUS: no frames are produced yet, so the widget shows its empty
/// state -- a black field with a centred hint.
class ViewerWidget : public QWidget {
    Q_OBJECT

public:
    /// Compatibility names used by the existing command surface.
    enum class FitMode {
        FitInWindow,
        ActualSize,
    };
    Q_ENUM(FitMode)

    /// What the viewer is currently showing.
    enum class State {
        Empty,   ///< No media -- the branded placeholder.
        Loading, ///< An open is in progress.
        Loaded,  ///< Displaying decoded frames.
        Error,   ///< Showing why the last open failed.
    };
    Q_ENUM(State)

    explicit ViewerWidget(QWidget* parent = nullptr);
    ~ViewerWidget() override;

    /// Switches to the loading state and drops any frame on screen, so a failed
    /// open cannot leave the previous clip's picture visible.
    void setLoading();

    /// Switches to the error state with a user-facing message.
    void setError(const QString& message);

    /// Returns to the empty state.
    void setEmpty();

    State state() const { return m_state; }

    /// Sets the frame to display and repaints. Moves the viewer into the
    /// Loaded state. An invalid frame clears it back to empty.
    void setFrame(const media::VideoFrame& frame);

    /// Source aspect ratio to preserve, accounting for non-square pixels.
    /// Defaults to the frame's own dimensions when not set.
    void setSourceAspectRatio(double ratio);
    void clear();

    FitMode fitMode() const { return m_transform.isFit() ? FitMode::FitInWindow : FitMode::ActualSize; }
    void setFitMode(FitMode mode);
    void fitImage();
    void showActualSize();
    void zoomIn();
    void zoomOut();
    void resetNavigationToFit();

    const ViewerTransform& transform() const { return m_transform; }
    void restoreTransform(const ViewerTransform& transform);
    void setVideoOnlyPresentation(bool enabled);
    bool videoOnlyPresentation() const { return m_videoOnlyPresentation; }

    /// Mirrors the picture left-to-right at paint time. Presentation only, like
    /// zoom and pan: it never touches decoding, timing or the frame itself, and
    /// it is deliberately not part of ViewerTransform, so snapshot/restore of
    /// navigation (Video Full Screen) cannot change it.
    void setFlipHorizontal(bool flipped);
    bool isFlippedHorizontally() const { return m_flipHorizontal; }

    /// Headline shown when there is nothing to display.
    void setPlaceholderText(const QString& text);

    /// Smaller line beneath the headline, for explaining why the viewer is
    /// empty. Pass an empty string to show the headline alone.
    void setPlaceholderSubtext(const QString& text);

    /// Label drawn in the corner, e.g. "A" or "B" during comparison.
    void setCornerLabel(const QString& label);

    QSize sizeHint() const override;

    // --- Annotations ------------------------------------------------------
    //
    // The viewer draws annotations over the picture and turns pointer and
    // tablet input into strokes, but owns no annotation data: it is told what
    // to show for the displayed frame and reports finished strokes by signal.
    // The owner (MainWindow) keeps the AnnotationStore.

    /// What to draw over the current picture. Call whenever the displayed
    /// frame or its annotations change.
    void setAnnotationFrame(const annotation::FrameAnnotation& frame);
    const annotation::FrameAnnotation& annotationFrame() const { return m_annotationFrame; }
    void setAnnotationsVisible(bool visible);
    bool annotationsVisible() const { return m_annotationsVisible; }

    /// Drawings from nearby frames to show faintly under this frame's
    /// (onion skin), for the frame `centerFrame`. Empty = no ghosts.
    void setGhostFrames(qint64 centerFrame, const QVector<annotation::GhostFrame>& ghosts);
    const QVector<annotation::GhostFrame>& ghostFrames() const { return m_ghosts; }

    /// Tracing paper: a white wash over the picture, under the drawings, so
    /// strokes read clearly (0 = off, 100 = blank paper). View only.
    void setTracingPaper(int opacityPercent);
    int tracingPaper() const { return m_tracingPaperPercent; }

    /// In annotate mode the left button / pen tip draws instead of doing
    /// nothing; middle-drag pan and wheel zoom keep working.
    void setAnnotateMode(bool enabled);
    bool annotateMode() const { return m_annotateMode; }
    void setAnnotationTool(const annotation::AnnotationToolSettings& settings);
    const annotation::AnnotationToolSettings& annotationTool() const { return m_tool; }

    /// Maps a widget point to normalised source-image coordinates (0..1 over
    /// the picture, unflipped), and back. Outside the picture gives values
    /// outside 0..1.
    QPointF widgetToNormalized(const QPointF& point) const;
    QPointF normalizedToWidget(const QPointF& normalized) const;

    bool isDrawing() const { return m_stroke.has_value(); }
    /// The in-progress item, for tests and the live preview.
    std::optional<annotation::AnnotationItem> strokePreview() const;

    /// Item picked by the Select tool on the displayed frame, or 0.
    quint64 selectedItemId() const { return m_selectedItemId; }
    void clearSelection();

    /// In-place text editing (Text tool, or double-click on text with Select).
    /// The editor is a child text field over the picture; committing emits
    /// annotationItemDrawn (new text) or annotationItemChanged (edited text).
    bool isEditingText() const { return m_textEdit.has_value(); }
    QTextEdit* textEditor() const { return m_textEditor; }
    void commitTextEdit();
    void cancelTextEdit();

signals:
    void zoomChanged(qreal percent, bool fitMode);
    void activated();

    /// A stroke, shape or text anchor was completed on `frameIndex` (the
    /// zero-based index of the frame displayed when the stroke started).
    void annotationItemDrawn(qint64 frameIndex, const atk::annotation::AnnotationItem& item);
    /// An eraser swipe begins / ends on `frameIndex`; the owner groups the
    /// erase requests in between into one undo step.
    void annotationEraseStarted(qint64 frameIndex);
    void annotationEraseFinished();
    /// Erase discs of `radius` image pixels at `centers` (normalised) on
    /// `frameIndex`. `imageSize` is the picture's size in image pixels.
    void annotationEraseRequested(qint64 frameIndex, const QVector<QPointF>& centers, qreal radius,
                                  const QSizeF& imageSize);
    /// An existing item (same id) was moved or its text edited.
    void annotationItemChanged(qint64 frameIndex, const atk::annotation::AnnotationItem& item);
    /// The selected item should be deleted (Delete / Backspace with Select).
    void annotationItemDeleteRequested(qint64 frameIndex, quint64 itemId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Where the picture lands inside the widget for the current fit mode.
    QSizeF displaySourceSize() const;
    void syncTransformSource(bool resetToFit = false);
    void notifyNavigationChanged();
    void paintEmptyState(QPainter& painter);
    void paintMessage(QPainter& painter, const QString& headline, const QString& detail);

    // Annotation input and painting.
    struct ActiveStroke {
        enum class Kind { Draw, Erase, Move, TextBox, TextFrame };
        Kind kind = Kind::Draw;
        qint64 frameIndex = -1;
        bool fromTablet = false;
        annotation::AnnotationItem item;                    // Draw: the item; Move: the original
        std::shared_ptr<annotation::StrokeBuilder> builder; // freehand only
        QPointF pressWidgetPosition;
        QPointF lastWidgetPosition; // eraser: previous sample, to sweep the gap
        QPointF pressNormalized;    // Move / TextBox
        QPointF currentNormalized;  // Move / TextBox
        int handle = -1;            // TextFrame: 0..7 resize handle, 8 = move by the border
        bool isEraser() const { return kind == Kind::Erase; }
    };
    struct TextEditState {
        qint64 frameIndex = -1;
        annotation::AnnotationItem item; // id 0 for new text
        bool existing = false;
    };
    void cancelStroke();
    quint64 itemAt(const QPointF& widgetPosition, bool textOnly) const;
    std::optional<annotation::AnnotationItem> findItem(quint64 id) const;
    std::optional<annotation::AnnotationItem> movePreview() const;
    void beginTextEdit(qint64 frameIndex, const annotation::AnnotationItem& item, bool existing);
    void finishTextEdit(bool commit);
    void positionTextEditor();
    void applyTextEditStyle();
    /// Frame drawn around the text being typed, in widget pixels; its border
    /// and handles sit just outside the text field so the viewer receives them.
    QRectF textEditFrame() const;
    /// 0..7: resize handle (clockwise from top-left), 8: border, -1: neither.
    int textFrameHandleAt(const QPointF& position) const;
    QPointF unflippedNormalized(const QPointF& position) const;
    bool isDoublePress(const QPointF& position, quint64 itemId);
    bool canAnnotate() const;
    bool beginStroke(const QPointF& position, float pressure, float tiltX, float tiltY,
                     bool eraserPointer, bool fromTablet);
    void continueStroke(const QPointF& position, float pressure, float tiltX, float tiltY);
    void finishStroke();
    void eraseAlong(const QPointF& from, const QPointF& to, float pressure);
    qreal widgetPixelsPerSourcePixel() const;
    qreal sourceImageHeight() const;
    void updateAnnotateCursor();
    void setHoverPosition(std::optional<QPointF> position);
    void paintAnnotations(QPainter& painter, const QRectF& target);
    void paintBrushCursor(QPainter& painter);
    /// Draws the picture (and tracing paper) through a widget-sized cache, so
    /// a repaint while drawing is one blit instead of a scaled draw and blend.
    void paintCachedPicture(QPainter& painter, const QRectF& target, const QColor& background);
    /// Widget area the brush cursor occupies at `position`.
    QRect brushCursorRect(const QPointF& position) const;
    /// Widget area the newest part of a freehand stroke occupies.
    QRect freehandDirtyRect() const;

    media::VideoFrame m_frame;
    State m_state = State::Empty;
    QString m_errorMessage;
    double m_sourceAspectRatio = 0.0;
    ViewerTransform m_transform;
    bool m_videoOnlyPresentation = false;
    bool m_flipHorizontal = false;
    bool m_middlePanning = false;
    QPointF m_lastPanPosition;
    QString m_placeholderText;
    QString m_placeholderSubtext;
    QString m_cornerLabel;

    annotation::FrameAnnotation m_annotationFrame;
    bool m_annotationsVisible = true;
    bool m_annotateMode = false;
    annotation::AnnotationToolSettings m_tool;
    std::optional<ActiveStroke> m_stroke;
    std::optional<QPointF> m_hoverPosition;
    bool m_hoverIsEraser = false; // stylus flipped to its eraser end
    quint64 m_selectedItemId = 0;
    quint64 m_hiddenItemId = 0; // drawn by a preview (move) or the editor instead
    std::optional<TextEditState> m_textEdit;
    QTextEdit* m_textEditor = nullptr; // child; deleted when editing ends
    QElapsedTimer m_lastPressTimer; // double-press detection that also works for pens
    QPointF m_lastPressPosition;
    quint64 m_lastPressItemId = 0;
    // Committed annotations rendered once per change of data or geometry, so
    // drawing a new stroke does not re-render every earlier one per event.
    QImage m_annotationCache;
    QRectF m_annotationCacheTarget;
    bool m_annotationCacheFlipped = false;
    quint64 m_annotationGeneration = 0;
    quint64 m_annotationCacheGeneration = ~quint64(0);
    // When the only change since the cache was drawn is items added on top
    // (a finished stroke), the cache is extended rather than redrawn: it holds
    // the first m_annotationCacheItems items, and m_annotationAppendGeneration
    // names the generation that extending it brings it up to.
    int m_annotationCacheItems = 0;
    quint64 m_annotationAppendGeneration = ~quint64(0);
    // Ghosts, composited once per change like the annotation cache.
    QVector<annotation::GhostFrame> m_ghosts;
    qint64 m_ghostCenterFrame = -1;
    QImage m_ghostCache;
    QRectF m_ghostCacheTarget;
    bool m_ghostCacheFlipped = false;
    quint64 m_ghostGeneration = 0;
    quint64 m_ghostCacheGeneration = ~quint64(0);
    int m_tracingPaperPercent = 0;
    // Picture + tracing paper at widget resolution (see paintCachedPicture).
    QImage m_pictureCache;
    qint64 m_pictureCacheKey = 0;
    QRectF m_pictureCacheTarget;
    bool m_pictureCacheFlipped = false;
    int m_pictureCacheTrace = -1;
    bool m_pictureCacheSmooth = false;
    QRgb m_pictureCacheBackground = 0;
    QRect m_lastStrokeDirty; // repainted again on the next move
};

} // namespace atk::ui
