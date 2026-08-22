#pragma once

#include "media/VideoFrame.h"
#include "ui/ViewerTransform.h"

#include <QWidget>

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

    /// Headline shown when there is nothing to display.
    void setPlaceholderText(const QString& text);

    /// Smaller line beneath the headline, for explaining why the viewer is
    /// empty. Pass an empty string to show the headline alone.
    void setPlaceholderSubtext(const QString& text);

    /// Label drawn in the corner, e.g. "A" or "B" during comparison.
    void setCornerLabel(const QString& label);

    QSize sizeHint() const override;

signals:
    void zoomChanged(qreal percent, bool fitMode);
    void activated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    /// Where the picture lands inside the widget for the current fit mode.
    QSizeF displaySourceSize() const;
    void syncTransformSource(bool resetToFit = false);
    void notifyNavigationChanged();
    void paintEmptyState(QPainter& painter);
    void paintMessage(QPainter& painter, const QString& headline, const QString& detail);

    media::VideoFrame m_frame;
    State m_state = State::Empty;
    QString m_errorMessage;
    double m_sourceAspectRatio = 0.0;
    ViewerTransform m_transform;
    bool m_videoOnlyPresentation = false;
    bool m_middlePanning = false;
    QPointF m_lastPanPosition;
    QString m_placeholderText;
    QString m_placeholderSubtext;
    QString m_cornerLabel;
};

} // namespace atk::ui
