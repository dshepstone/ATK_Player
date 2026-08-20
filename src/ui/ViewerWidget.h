#pragma once

#include "media/VideoFrame.h"

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
    /// How the picture is fitted into the widget.
    enum class FitMode {
        FitInWindow, ///< scale down to fit, never scale up past 1:1
        ActualSize,  ///< 1:1 pixels, centred
        Stretch,     ///< fill, ignoring aspect ratio
    };
    Q_ENUM(FitMode)

    explicit ViewerWidget(QWidget* parent = nullptr);
    ~ViewerWidget() override;

    /// Sets the frame to display and repaints. An invalid frame clears the
    /// viewer back to its empty state.
    void setFrame(const media::VideoFrame& frame);
    void clear();

    FitMode fitMode() const { return m_fitMode; }
    void setFitMode(FitMode mode);

    /// Headline shown when there is nothing to display.
    void setPlaceholderText(const QString& text);

    /// Smaller line beneath the headline, for explaining why the viewer is
    /// empty. Pass an empty string to show the headline alone.
    void setPlaceholderSubtext(const QString& text);

    /// Label drawn in the corner, e.g. "A" or "B" during comparison.
    void setCornerLabel(const QString& label);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Where the picture lands inside the widget for the current fit mode.
    QRect targetRectFor(const QSize& imageSize) const;
    void paintEmptyState(QPainter& painter);

    media::VideoFrame m_frame;
    FitMode m_fitMode = FitMode::FitInWindow;
    QString m_placeholderText;
    QString m_placeholderSubtext;
    QString m_cornerLabel;
};

} // namespace atk::ui
