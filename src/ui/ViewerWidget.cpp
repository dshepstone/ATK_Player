#include "ui/ViewerWidget.h"

#include "annotation/AnnotationRenderer.h"
#include "annotation/StrokeBuilder.h"
#include "core/Logging.h"
#include "ui/Theme.h"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <algorithm>
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
    m_stroke.reset();
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

void ViewerWidget::setFlipHorizontal(bool flipped)
{
    if (m_flipHorizontal == flipped) return;
    m_flipHorizontal = flipped;
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
            const QRectF target = m_transform.imageRect();
            if (m_flipHorizontal) {
                // Mirror about the picture's own centre line, so the image
                // occupies exactly the rect zoom and pan computed.
                painter.save();
                painter.translate(target.left() + target.right(), 0.0);
                painter.scale(-1.0, 1.0);
            }
            painter.drawImage(target, m_frame.image);
            if (m_flipHorizontal) {
                painter.restore();
            }
            paintAnnotations(painter, target);
            paintBrushCursor(painter);
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
    if (!m_videoOnlyPresentation && m_flipHorizontal && m_state == State::Loaded) {
        // A mirrored picture must never be mistaken for the real one.
        painter.setPen(theme::textSecondary());
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(rect().adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::AlignRight,
                         tr("FLIPPED H"));
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
    emit activated();
    if (event->button() == Qt::MiddleButton) {
        m_middlePanning = true;
        m_lastPanPosition = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_annotateMode) {
        // A tablet stroke in progress owns the pointer; ignore any mouse
        // events a driver might still synthesise for it.
        if (!(m_stroke && m_stroke->fromTablet))
            beginStroke(event->position(), 1.0f, 0.0f, 0.0f, false, false);
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
    if (m_annotateMode) {
        setHoverPosition(event->position());
        if (m_stroke && !m_stroke->fromTablet)
            continueStroke(event->position(), 1.0f, 0.0f, 0.0f);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_middlePanning) {
        m_middlePanning = false;
        updateAnnotateCursor();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_stroke && !m_stroke->fromTablet) {
        finishStroke();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewerWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_annotateMode) {
        // While drawing, a quick double tap is two dots, not "fit".
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        fitImage();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ViewerWidget::tabletEvent(QTabletEvent* event)
{
    // Outside annotate mode, or while panning with a barrel button, let Qt
    // synthesise mouse events so the viewer behaves exactly as with a mouse.
    if (!m_annotateMode || m_middlePanning) {
        event->ignore();
        return;
    }
    const QPointF position = event->position();
    const bool eraserPointer = event->pointerType() == QPointingDevice::PointerType::Eraser;
    switch (event->type()) {
    case QEvent::TabletPress:
        // Barrel buttons fall through to mouse synthesis (middle-drag pan).
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }
        emit activated();
        if (beginStroke(position, float(event->pressure()), float(event->xTilt()),
                        float(event->yTilt()), eraserPointer, true)) {
            event->accept();
        } else {
            event->ignore();
        }
        return;
    case QEvent::TabletMove:
        // Accepting hover moves too keeps Qt from synthesising mouse moves,
        // and lets the brush cursor follow the pen in proximity.
        setHoverPosition(position);
        if (m_stroke && m_stroke->fromTablet)
            continueStroke(position, float(event->pressure()), float(event->xTilt()),
                           float(event->yTilt()));
        event->accept();
        return;
    case QEvent::TabletRelease:
        if (event->button() == Qt::LeftButton && m_stroke && m_stroke->fromTablet) {
            finishStroke();
            event->accept();
            return;
        }
        event->ignore();
        return;
    default:
        event->ignore();
        return;
    }
}

void ViewerWidget::leaveEvent(QEvent* event)
{
    setHoverPosition(std::nullopt);
    QWidget::leaveEvent(event);
}

// --- Annotations ---------------------------------------------------------

void ViewerWidget::setAnnotationFrame(const annotation::FrameAnnotation& frame)
{
    m_annotationFrame = frame;
    ++m_annotationGeneration;
    update();
}

void ViewerWidget::setAnnotationsVisible(bool visible)
{
    if (m_annotationsVisible == visible) return;
    m_annotationsVisible = visible;
    update();
}

void ViewerWidget::setAnnotateMode(bool enabled)
{
    if (m_annotateMode == enabled) return;
    m_annotateMode = enabled;
    if (!enabled) {
        // Leaving the mode abandons an unfinished stroke rather than committing
        // something the reviewer did not finish.
        m_stroke.reset();
        m_hoverPosition.reset();
    }
    setMouseTracking(enabled);
    setAttribute(Qt::WA_TabletTracking, enabled);
    updateAnnotateCursor();
    update();
}

void ViewerWidget::setAnnotationTool(const annotation::AnnotationToolSettings& settings)
{
    m_tool = settings;
    updateAnnotateCursor();
    update();
}

QPointF ViewerWidget::widgetToNormalized(const QPointF& point) const
{
    const QRectF target = m_transform.imageRect();
    if (target.width() <= 0.0 || target.height() <= 0.0) return {-1.0, -1.0};
    QPointF p = point;
    // The picture is mirrored about its own centre line; undo that so stored
    // coordinates always refer to the real, unflipped image.
    if (m_flipHorizontal) p.setX(target.left() + target.right() - p.x());
    return {(p.x() - target.left()) / target.width(), (p.y() - target.top()) / target.height()};
}

QPointF ViewerWidget::normalizedToWidget(const QPointF& normalized) const
{
    const QRectF target = m_transform.imageRect();
    QPointF p(target.left() + normalized.x() * target.width(),
              target.top() + normalized.y() * target.height());
    if (m_flipHorizontal) p.setX(target.left() + target.right() - p.x());
    return p;
}

std::optional<annotation::AnnotationItem> ViewerWidget::strokePreview() const
{
    if (!m_stroke || m_stroke->eraser) return std::nullopt;
    annotation::AnnotationItem item = m_stroke->item;
    if (m_stroke->builder) item.points = m_stroke->builder->points();
    return item;
}

bool ViewerWidget::canAnnotate() const
{
    return m_annotateMode && m_state == State::Loaded && m_frame.isValid();
}

qreal ViewerWidget::sourceImageHeight() const
{
    const QSizeF source = displaySourceSize();
    return source.height() > 0.0 ? source.height() : 1080.0;
}

qreal ViewerWidget::widgetPixelsPerSourcePixel() const
{
    const QRectF target = m_transform.imageRect();
    return target.height() > 0.0 ? target.height() / sourceImageHeight() : 1.0;
}

bool ViewerWidget::beginStroke(const QPointF& position, float pressure, float tiltX, float tiltY,
                               bool eraserPointer, bool fromTablet)
{
    using annotation::AnnotationTool;
    if (!canAnnotate()) return false;
    const QPointF n = widgetToNormalized(position);
    const bool inside = n.x() >= 0.0 && n.x() <= 1.0 && n.y() >= 0.0 && n.y() <= 1.0;

    ActiveStroke stroke;
    stroke.frameIndex = m_frame.frameIndex;
    stroke.fromTablet = fromTablet;
    stroke.pressWidgetPosition = position;

    if (eraserPointer || m_tool.tool == AnnotationTool::Eraser) {
        stroke.eraser = true;
        stroke.lastWidgetPosition = position;
        m_stroke = stroke;
        eraseAt(position);
        update();
        return true;
    }
    // Drawing starts on the picture; a press on the letterbox does nothing.
    if (!inside) return false;

    if (m_tool.tool == AnnotationTool::Text) {
        emit annotationTextRequested(stroke.frameIndex, n);
        return true;
    }

    stroke.item = m_tool.makeItem(sourceImageHeight());
    if (annotation::isShapeTool(m_tool.tool)) {
        const annotation::StrokePoint p{float(n.x()), float(n.y()), 1.0f, 0.0f, 0.0f};
        stroke.item.points = {p, p};
    } else {
        annotation::StrokeBuilder::Settings settings;
        settings.curve = m_tool.pressureCurve;
        settings.usePressure = m_tool.usePressure;
        settings.stabilizer = m_tool.stabilizer;
        // Roughly one stored point per screen pixel of movement, whatever the zoom.
        settings.minimumSpacing = std::max(0.25, 0.75 / widgetPixelsPerSourcePixel());
        stroke.builder = std::make_shared<annotation::StrokeBuilder>(settings, displaySourceSize());
        stroke.builder->addSample(n, pressure, tiltX, tiltY);
    }
    m_stroke = std::move(stroke);
    update();
    return true;
}

void ViewerWidget::continueStroke(const QPointF& position, float pressure, float tiltX, float tiltY)
{
    if (!m_stroke) return;
    if (m_stroke->eraser) {
        eraseAlong(m_stroke->lastWidgetPosition, position);
        m_stroke->lastWidgetPosition = position;
        update();
        return;
    }
    const QPointF n = widgetToNormalized(position);
    if (m_stroke->builder) {
        m_stroke->builder->addSample(n, pressure, tiltX, tiltY);
    } else if (m_stroke->item.points.size() == 2) {
        m_stroke->item.points[1].x = float(std::clamp(n.x(), 0.0, 1.0));
        m_stroke->item.points[1].y = float(std::clamp(n.y(), 0.0, 1.0));
    }
    update();
}

void ViewerWidget::finishStroke()
{
    if (!m_stroke) return;
    ActiveStroke stroke = std::move(*m_stroke);
    m_stroke.reset();
    if (!stroke.eraser) {
        if (stroke.builder) {
            stroke.item.points = stroke.builder->finish();
        } else if (stroke.item.points.size() == 2) {
            // A click without a drag is not a shape.
            const QPointF a = normalizedToWidget({stroke.item.points[0].x, stroke.item.points[0].y});
            const QPointF b = normalizedToWidget({stroke.item.points[1].x, stroke.item.points[1].y});
            if (std::hypot(a.x() - b.x(), a.y() - b.y()) < 3.0) stroke.item.points.clear();
        }
        if (annotation::isValidItem(stroke.item))
            emit annotationItemDrawn(stroke.frameIndex, stroke.item);
    }
    update();
}

void ViewerWidget::eraseAt(const QPointF& position)
{
    eraseAlong(position, position);
}

void ViewerWidget::eraseAlong(const QPointF& from, const QPointF& to)
{
    const qint64 frameIndex = m_stroke ? m_stroke->frameIndex : m_frame.frameIndex;
    if (m_annotationFrame.isEmpty() || m_annotationFrame.frame != frameIndex) return;
    const QRectF target = m_transform.imageRect();
    const qreal tolerance = std::max<qreal>(3.0, 0.5 * m_tool.sizePixels * widgetPixelsPerSourcePixel());
    // Pointer samples can be far apart on a fast drag; test the path between
    // them at intervals no wider than the eraser, so it cannot jump a line.
    const qreal distance = std::hypot(to.x() - from.x(), to.y() - from.y());
    const int steps = std::clamp(int(std::ceil(distance / tolerance)), 1, 512);
    QVector<quint64> hits;
    for (int s = 0; s <= steps; ++s) {
        QPointF p = from + (to - from) * (qreal(s) / steps);
        if (m_flipHorizontal) p.setX(target.left() + target.right() - p.x());
        for (const annotation::AnnotationItem& item : m_annotationFrame.items)
            if (!hits.contains(item.id) && annotation::AnnotationRenderer::hitTest(target, item, p, tolerance))
                hits.append(item.id);
    }
    if (!hits.isEmpty()) emit annotationEraseRequested(frameIndex, hits);
}

void ViewerWidget::updateAnnotateCursor()
{
    if (m_middlePanning) return;
    if (!m_annotateMode) {
        unsetCursor();
        return;
    }
    using annotation::AnnotationTool;
    const bool brush = annotation::isFreehandTool(m_tool.tool) || m_tool.tool == AnnotationTool::Eraser;
    // Freehand tools draw their own size circle, so the arrow would only hide it.
    setCursor(brush ? Qt::BlankCursor : Qt::CrossCursor);
}

void ViewerWidget::setHoverPosition(std::optional<QPointF> position)
{
    if (m_hoverPosition == position) return;
    m_hoverPosition = position;
    if (m_annotateMode) update();
}

void ViewerWidget::paintAnnotations(QPainter& painter, const QRectF& target)
{
    if (!m_annotationsVisible) return;
    auto applyFlip = [&](QPainter& p) {
        if (m_flipHorizontal) {
            p.translate(target.left() + target.right(), 0.0);
            p.scale(-1.0, 1.0);
        }
    };
    // Only draw annotations that belong to the frame on screen.
    if (!m_annotationFrame.isEmpty() && m_annotationFrame.frame == m_frame.frameIndex) {
        const qreal dpr = devicePixelRatioF();
        const QSize pixelSize = (QSizeF(size()) * dpr).toSize();
        if (m_annotationCacheGeneration != m_annotationGeneration || m_annotationCacheTarget != target
            || m_annotationCacheFlipped != m_flipHorizontal || m_annotationCache.size() != pixelSize
            || !qFuzzyCompare(m_annotationCache.devicePixelRatio(), dpr)) {
            m_annotationCache = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
            m_annotationCache.setDevicePixelRatio(dpr);
            m_annotationCache.fill(Qt::transparent);
            QPainter cachePainter(&m_annotationCache);
            applyFlip(cachePainter);
            annotation::AnnotationRenderer::paintFrame(cachePainter, target, m_annotationFrame);
            m_annotationCacheGeneration = m_annotationGeneration;
            m_annotationCacheTarget = target;
            m_annotationCacheFlipped = m_flipHorizontal;
        }
        painter.drawImage(QPointF(0.0, 0.0), m_annotationCache);
    }
    if (const auto preview = strokePreview(); preview && annotation::isValidItem(*preview)) {
        painter.save();
        applyFlip(painter);
        annotation::AnnotationRenderer::paintItem(painter, target, *preview);
        painter.restore();
    }
}

void ViewerWidget::paintBrushCursor(QPainter& painter)
{
    using annotation::AnnotationTool;
    if (!m_annotateMode || !m_hoverPosition || m_middlePanning) return;
    if (!annotation::isFreehandTool(m_tool.tool) && m_tool.tool != AnnotationTool::Eraser) return;
    const qreal radius = std::max<qreal>(1.5, 0.5 * m_tool.sizePixels * widgetPixelsPerSourcePixel());
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    // Dark ring outside a light ring: visible over any picture.
    painter.setPen(QPen(QColor(0, 0, 0, 170), 1.0));
    painter.drawEllipse(*m_hoverPosition, radius + 1.0, radius + 1.0);
    painter.setPen(QPen(QColor(255, 255, 255, 220), 1.0));
    painter.drawEllipse(*m_hoverPosition, radius, radius);
    painter.restore();
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
