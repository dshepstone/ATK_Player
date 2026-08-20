#include "ui/ViewerWidget.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

namespace atk::ui {

ViewerWidget::ViewerWidget(QWidget* parent)
    : QWidget(parent)
    , m_placeholderText(tr("ATK PLAYER"))
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
    if (!frame.isValid()) {
        clear();
        return;
    }
    m_frame = frame;
    m_state = State::Loaded;
    m_errorMessage.clear();
    update();
}

void ViewerWidget::clear()
{
    m_frame = media::VideoFrame{};
    m_state = State::Empty;
    update();
}

void ViewerWidget::setLoading()
{
    // Dropping the frame here is what stops a failed open from leaving the
    // previous clip's picture on screen under a new file's name.
    m_frame = media::VideoFrame{};
    m_errorMessage.clear();
    m_state = State::Loading;
    update();
}

void ViewerWidget::setError(const QString& message)
{
    m_frame = media::VideoFrame{};
    m_errorMessage = message;
    m_state = State::Error;
    update();
}

void ViewerWidget::setEmpty()
{
    clear();
}

void ViewerWidget::setSourceAspectRatio(double ratio)
{
    if (qFuzzyCompare(m_sourceAspectRatio, ratio)) {
        return;
    }
    m_sourceAspectRatio = ratio > 0.0 ? ratio : 0.0;
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

void ViewerWidget::setPlaceholderSubtext(const QString& text)
{
    m_placeholderSubtext = text;
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

    // Non-square source pixels (anamorphic material) must be corrected here,
    // or the picture is displayed at the wrong shape.
    QSize displaySize = imageSize;
    if (m_sourceAspectRatio > 0.0) {
        const double imageRatio =
            static_cast<double>(imageSize.width()) / static_cast<double>(imageSize.height());
        if (!qFuzzyCompare(imageRatio, m_sourceAspectRatio)) {
            displaySize.setWidth(
                qRound(static_cast<double>(imageSize.height()) * m_sourceAspectRatio));
        }
    }

    switch (m_fitMode) {
    case FitMode::Stretch:
        return bounds;

    case FitMode::ActualSize: {
        // 1:1 pixels, centred. A frame larger than the widget is allowed to
        // overhang; scrolling to the region of interest arrives with pan/zoom
        // in milestone M2.
        const QPoint topLeft(bounds.x() + (bounds.width() - displaySize.width()) / 2,
                             bounds.y() + (bounds.height() - displaySize.height()) / 2);
        return { topLeft, displaySize };
    }

    case FitMode::FitInWindow:
    default: {
        QSize scaled = displaySize;
        scaled.scale(bounds.size(), Qt::KeepAspectRatio);
        // Never upscale past 1:1 -- magnifying a frame silently would misrepresent
        // the material being reviewed.
        if (scaled.width() > displaySize.width()) {
            scaled = displaySize;
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

    switch (m_state) {
    case State::Loaded:
        if (m_frame.isValid()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(targetRectFor(m_frame.image.size()), m_frame.image);
        }
        break;

    case State::Loading:
        paintMessage(painter, tr("Loading media..."), QString());
        break;

    case State::Error:
        paintMessage(painter, tr("Could not open media"), m_errorMessage);
        break;

    case State::Empty:
        paintEmptyState(painter);
        break;
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
    paintMessage(painter, m_placeholderText, m_placeholderSubtext);
}

void ViewerWidget::paintMessage(QPainter& painter, const QString& headline, const QString& detail)
{
    const QFont baseFont = painter.font();

    // The headline is drawn larger and letter-spaced; the detail line sits
    // beneath it in the muted colour.
    //
    // The application stylesheet sets font-size in pixels, so pointSizeF()
    // returns -1 and scaling it would produce an invalid font. Scale whichever
    // unit the font actually carries.
    QFont headlineFont = baseFont;
    constexpr qreal kHeadlineScale = 1.9;

    if (baseFont.pointSizeF() > 0.0) {
        headlineFont.setPointSizeF(baseFont.pointSizeF() * kHeadlineScale);
    } else if (baseFont.pixelSize() > 0) {
        headlineFont.setPixelSize(qRound(baseFont.pixelSize() * kHeadlineScale));
    }

    headlineFont.setLetterSpacing(QFont::PercentageSpacing, 145);
    headlineFont.setWeight(QFont::Light);

    painter.setFont(headlineFont);
    const QFontMetrics headlineMetrics(headlineFont);
    const int headlineHeight = headlineMetrics.height();

    // Both lines are centred as a block, so the headline sits slightly above
    // centre when a subtext is present and dead centre when it is not.
    const int blockHeight = detail.isEmpty()
        ? headlineHeight
        : headlineHeight + QFontMetrics(baseFont).height() + 10;

    const int top = rect().top() + (rect().height() - blockHeight) / 2;

    painter.setPen(theme::textSecondary());
    painter.drawText(QRect(rect().left(), top, rect().width(), headlineHeight),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     headline);

    if (!detail.isEmpty()) {
        painter.setFont(baseFont);
        painter.setPen(theme::textDisabled());
        painter.drawText(QRect(rect().left(), top + headlineHeight + 10,
                               rect().width(), QFontMetrics(baseFont).height()),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         detail);
    }

    painter.setFont(baseFont);
}

} // namespace atk::ui
