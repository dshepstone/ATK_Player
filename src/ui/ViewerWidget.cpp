#include "ui/ViewerWidget.h"

#include "core/Logging.h"
#include "ui/Theme.h"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <cmath>

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
    m_transform.setDevicePixelRatio(devicePixelRatioF());
    m_transform.setViewportSize(size());
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
    syncTransformSource();
    qCDebug(log::ui) << "Viewer update frame" << frame.frameIndex;
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
    syncTransformSource();
    update();
}

void ViewerWidget::setFitMode(FitMode mode)
{
    if (mode == FitMode::FitInWindow) fitImage();
    else showActualSize();
}

void ViewerWidget::fitImage()
{
    m_transform.fit();
    notifyNavigationChanged();
    update();
}

void ViewerWidget::showActualSize()
{
    m_transform.setActualSize();
    notifyNavigationChanged();
    update();
}

void ViewerWidget::zoomIn()
{
    m_transform.zoomAt(ViewerTransform::kWheelStepFactor, rect().center());
    notifyNavigationChanged();
    update();
}

void ViewerWidget::zoomOut()
{
    m_transform.zoomAt(1.0 / ViewerTransform::kWheelStepFactor, rect().center());
    notifyNavigationChanged();
    update();
}

void ViewerWidget::resetNavigationToFit()
{
    syncTransformSource(true);
    m_transform.fit();
    notifyNavigationChanged();
    update();
}

void ViewerWidget::restoreTransform(const ViewerTransform& transform)
{
    m_transform = transform;
    m_transform.setDevicePixelRatio(devicePixelRatioF());
    m_transform.setViewportSize(size());
    notifyNavigationChanged();
    update();
}

void ViewerWidget::setVideoOnlyPresentation(bool enabled)
{
    if (m_videoOnlyPresentation == enabled) return;
    m_videoOnlyPresentation = enabled;
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

QSizeF ViewerWidget::displaySourceSize() const
{
    if (!m_frame.isValid()) return {};
    QSizeF displaySize = m_frame.image.size();
    if (m_sourceAspectRatio > 0.0) {
        const double imageRatio =
            static_cast<double>(m_frame.image.width()) / static_cast<double>(m_frame.image.height());
        if (!qFuzzyCompare(imageRatio, m_sourceAspectRatio)) {
            displaySize.setWidth(
                static_cast<double>(m_frame.image.height()) * m_sourceAspectRatio);
        }
    }
    return displaySize;
}

void ViewerWidget::syncTransformSource(bool resetToFit)
{
    const QSizeF oldSource = m_transform.sourceSize();
    const qreal oldZoom = m_transform.zoomRatio();
    const bool oldFit = m_transform.isFit();
    m_transform.setDevicePixelRatio(devicePixelRatioF());
    m_transform.setViewportSize(size());
    m_transform.setSourceSize(displaySourceSize(), resetToFit);
    if (oldSource != m_transform.sourceSize()
        || !qFuzzyCompare(oldZoom, m_transform.zoomRatio())
        || oldFit != m_transform.isFit()) {
        notifyNavigationChanged();
    }
}

void ViewerWidget::notifyNavigationChanged()
{
    emit zoomChanged(m_transform.zoomRatio() * 100.0, m_transform.isFit());
}

void ViewerWidget::paintEvent(QPaintEvent* event)
{
    QElapsedTimer paintTimer;
    paintTimer.start();
    QPainter painter(this);
    painter.fillRect(event->rect(), m_videoOnlyPresentation ? Qt::black
                                                            : theme::viewerBackground());

    switch (m_state) {
    case State::Loaded:
        if (m_frame.isValid()) {
            // Smooth minification keeps fitted playback clean; above 100%,
            // nearest-like sampling exposes source pixels for frame inspection.
            painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                  m_transform.zoomRatio() <= 1.0);
            painter.drawImage(m_transform.imageRect(), m_frame.image);
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

    if (!m_videoOnlyPresentation && !m_cornerLabel.isEmpty()) {
        painter.setPen(theme::textSecondary());
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(rect().adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::AlignLeft, m_cornerLabel);
    }
    if (m_frame.isValid()) {
        qCDebug(log::ui) << "Viewer painted frame" << m_frame.frameIndex
                         << "in us" << paintTimer.nsecsElapsed() / 1000;
    }
}

void ViewerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_transform.setDevicePixelRatio(devicePixelRatioF());
    m_transform.setViewportSize(event->size());
    notifyNavigationChanged();
}

void ViewerWidget::wheelEvent(QWheelEvent* event)
{
    qreal steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps) && !event->pixelDelta().isNull()) {
        steps = event->pixelDelta().y() / 100.0;
    }
    if (!qFuzzyIsNull(steps)) {
        m_transform.zoomAt(std::pow(ViewerTransform::kWheelStepFactor, steps), event->position());
        notifyNavigationChanged();
        update();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void ViewerWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        m_middlePanning = true;
        m_lastPanPosition = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_middlePanning) {
        m_transform.panBy(event->position() - m_lastPanPosition);
        m_lastPanPosition = event->position();
        notifyNavigationChanged();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_middlePanning) {
        m_middlePanning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewerWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        fitImage();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
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
