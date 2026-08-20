#include "ui/ViewerWidget.h"

#include "ui/Theme.h"

#include <QPaintEvent>
#include <QPainter>

namespace atk::ui {

ViewerWidget::ViewerWidget(QWidget* parent)
    : QWidget(parent)
    , m_placeholderText(tr("No media loaded"))
{
    // The viewer paints every pixel it owns, so let Qt skip the background fill.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
    setMinimumSize(320, 180);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

ViewerWidget::~ViewerWidget() = default;

QSize ViewerWidget::sizeHint() const
{
    return { 960, 540 };
}

void ViewerWidget::setFrame(const media::VideoFrame& frame)
{
    m_frame = frame;
    update();
}

void ViewerWidget::clear()
{
    m_frame = media::VideoFrame{};
    update();
}

void ViewerWidget::setFitMode(FitMode mode)
{
    if (m_fitMode == mode) {
        return;
    }
    m_fitMode = mode;
    update();
}

void ViewerWidget::setPlaceholderText(const QString& text)
{
    m_placeholderText = text;
    if (!m_frame.isValid()) {
        update();
    }
}

void ViewerWidget::setCornerLabel(const QString& label)
{
    m_cornerLabel = label;
    update();
}

QRect ViewerWidget::targetRectFor(const QSize& imageSize) const
{
    const QRect bounds = rect();
    if (imageSize.isEmpty()) {
        return bounds;
    }

    switch (m_fitMode) {
    case FitMode::Stretch:
        return bounds;

    case FitMode::ActualSize: {
        // 1:1 pixels, centred. A frame larger than the widget is allowed to
        // overhang; scrolling to the region of interest arrives with pan/zoom
        // in milestone M2.
        const QPoint topLeft(bounds.x() + (bounds.width() - imageSize.width()) / 2,
                             bounds.y() + (bounds.height() - imageSize.height()) / 2);
        return { topLeft, imageSize };
    }

    case FitMode::FitInWindow:
    default: {
        QSize scaled = imageSize;
        scaled.scale(bounds.size(), Qt::KeepAspectRatio);
        // Never upscale past 1:1 -- magnifying a frame silently would misrepresent
        // the material being reviewed.
        if (scaled.width() > imageSize.width()) {
            scaled = imageSize;
        }
        const QPoint topLeft(bounds.x() + (bounds.width() - scaled.width()) / 2,
                             bounds.y() + (bounds.height() - scaled.height()) / 2);
        return { topLeft, scaled };
    }
    }
}

void ViewerWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), theme::viewerBackground());

    if (m_frame.isValid()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(targetRectFor(m_frame.image.size()), m_frame.image);
    } else {
        paintEmptyState(painter);
    }

    if (!m_cornerLabel.isEmpty()) {
        painter.setPen(theme::textSecondary());
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(rect().adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::AlignLeft, m_cornerLabel);
    }
}

void ViewerWidget::paintEmptyState(QPainter& painter)
{
    painter.setPen(theme::textDisabled());
    painter.drawText(rect(), Qt::AlignCenter, m_placeholderText);
}

} // namespace atk::ui
