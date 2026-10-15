#include "ui/ViewerWidget.h"

#include "annotation/AnnotationRenderer.h"
#include "annotation/StrokeBuilder.h"
#include "core/Logging.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPixmap>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

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
    if (frame.frameIndex != m_frame.frameIndex) {
        // Text being typed belongs to the frame it started on; finish it there.
        commitTextEdit();
        m_selectedItemId = 0;
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
    commitTextEdit();
    m_selectedItemId = 0;
    m_frame = media::VideoFrame{};
    m_state = State::Empty;
    cancelStroke();
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

void ViewerWidget::zoomToPercent(qreal percent)
{
    m_transform.setZoomRatioAt(percent / 100.0, rect().center());
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
    positionTextEditor();
    emit zoomChanged(m_transform.zoomRatio() * 100.0, m_transform.isFit());
}

void ViewerWidget::paintEvent(QPaintEvent* event)
{
    QElapsedTimer paintTimer;
    paintTimer.start();
    QPainter painter(this);
    // An explicit clip lets the annotation renderer limit its work to the
    // area being repainted (a few pixels around the pen while drawing).
    painter.setClipRegion(event->region());
    const QColor background = m_videoOnlyPresentation ? QColor(Qt::black) : theme::viewerBackground();
    painter.fillRect(event->rect(), background);

    switch (m_state) {
    case State::Loaded:
        if (m_frame.isValid() && (m_annotateMode || m_tracingPaperPercent > 0)) {
            const QRectF target = m_transform.imageRect();
            paintCachedPicture(painter, target, background);
            paintAnnotations(painter, target);
            paintBrushCursor(painter);
        } else if (m_frame.isValid()) {
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
            if (m_tracingPaperPercent > 0) {
                // Tracing paper sits over the picture and under the drawings.
                painter.fillRect(target, QColor(255, 255, 255, m_tracingPaperPercent * 255 / 100));
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
        if (!(m_stroke && m_stroke->fromTablet)) m_hoverIsEraser = false;
        if (m_textEdit && !m_stroke) {
            static const Qt::CursorShape shapes[9] = {
                Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                Qt::SizeAllCursor};
            const int handle = textFrameHandleAt(event->position());
            if (handle >= 0) setCursor(shapes[handle]);
            else updateAnnotateCursor();
        } else if (!m_stroke) {
            updateSelectCursor(event->position());
        }
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
        // While drawing, a double click is a second press (two dots, or a
        // double-click on text with Select), never "fit". Platforms differ in
        // whether a plain press precedes this event; if that press already
        // opened the text editor, leave it open.
        if (m_textEdit || (m_stroke && m_stroke->fromTablet)) {
            event->accept();
            return;
        }
        // The system has already decided this is a double click: on text with
        // the Select tool, that means "edit".
        if (m_tool.tool == annotation::AnnotationTool::Select && canAnnotate()) {
            if (const auto text = findItem(itemAt(event->position(), true))) {
                m_selectedItemId = text->id;
                beginTextEdit(m_frame.frameIndex, *text, true);
                event->accept();
                return;
            }
        }
        beginStroke(event->position(), 1.0f, 0.0f, 0.0f, false, false);
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
        if (m_hoverIsEraser != eraserPointer) {
            m_hoverIsEraser = eraserPointer;
            update();
        }
        if (!m_stroke && !m_textEdit) updateSelectCursor(position);
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
    // The usual edit while drawing is one stroke added on top: the cached
    // strokes stay valid and only the new one needs painting. A note edit
    // leaves the drawings untouched altogether. The cache holds only the
    // items' pixels, so it also stays valid when a held drawing (the same
    // items) moves on to the next frame.
    const bool sameItems = frame.items == m_annotationFrame.items;
    const bool appended = !sameItems && frame.items.size() > m_annotationFrame.items.size()
        && std::equal(m_annotationFrame.items.cbegin(), m_annotationFrame.items.cend(), frame.items.cbegin());
    const quint64 hiddenBefore = m_hiddenItemId;
    m_annotationFrame = frame;
    // Any hidden item has now been replaced by the owner's updated data.
    if (!m_textEdit || !m_textEdit->existing) m_hiddenItemId = 0;
    if (m_selectedItemId && !findItem(m_selectedItemId)) m_selectedItemId = 0;
    if (m_hiddenItemId != hiddenBefore || !(sameItems || appended)) ++m_annotationGeneration;
    update();
}

void ViewerWidget::setGhostFrames(qint64 centerFrame, const QVector<annotation::GhostFrame>& ghosts)
{
    if (ghosts.isEmpty() && m_ghosts.isEmpty()) return;
    // Drawing on this frame re-sends the same ghosts; keep the composited cache.
    if (centerFrame == m_ghostCenterFrame && ghosts == m_ghosts) return;
    m_ghosts = ghosts;
    m_ghostCenterFrame = centerFrame;
    ++m_ghostGeneration;
    update();
}

void ViewerWidget::setTracingPaper(int opacityPercent)
{
    opacityPercent = std::clamp(opacityPercent, 0, 100);
    if (m_tracingPaperPercent == opacityPercent) return;
    m_tracingPaperPercent = opacityPercent;
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
        // something the reviewer did not finish; typed text is kept.
        cancelStroke();
        commitTextEdit();
        clearSelection();
        m_hoverPosition.reset();
    }
    setMouseTracking(enabled);
    setAttribute(Qt::WA_TabletTracking, enabled);
    updateAnnotateCursor();
    update();
}

void ViewerWidget::setAnnotationTool(const annotation::AnnotationToolSettings& settings)
{
    const bool toolChanged = settings.tool != m_tool.tool;
    m_tool = settings;
    if (toolChanged) {
        commitTextEdit();
        if (m_tool.tool != annotation::AnnotationTool::Select) clearSelection();
    } else if (m_textEdit) {
        // Size, colour and opacity changes apply to the text being typed, so
        // what is on screen is what will be committed.
        const annotation::AnnotationItem styled = m_tool.makeItem(sourceImageHeight());
        m_textEdit->item.size = styled.size;
        m_textEdit->item.color = styled.color;
        m_textEdit->item.opacity = styled.opacity;
        applyTextEditStyle();
        positionTextEditor();
    }
    updateAnnotateCursor();
    update();
}

bool ViewerWidget::selectItem(quint64 id)
{
    if (id == 0 || !findItem(id)) return false;
    if (m_selectedItemId != id) {
        m_selectedItemId = id;
        update();
    }
    return true;
}

void ViewerWidget::clearSelection()
{
    if (m_selectedItemId == 0) return;
    m_selectedItemId = 0;
    update();
}

namespace {
constexpr qreal kTransformHandleReach = 8.0; // widget pixels either side of a handle
constexpr qreal kRotateKnobOffset = 22.0;    // knob distance above the box's top edge
constexpr qreal kRotateZone = 22.0;          // just outside a corner also rotates
// The handle box is never thinner than this, so the handles of a thin stroke
// sit clear of its ink and a press on the ink still moves it.
constexpr qreal kMinimumBoxSize = 24.0;

annotation::TransformBox handleBox(annotation::TransformBox box)
{
    box.size = QSizeF(std::max(box.size.width(), kMinimumBoxSize), std::max(box.size.height(), kMinimumBoxSize));
    return box;
}

/// A curved arrow, since Qt has no rotate cursor.
QCursor rotateCursor()
{
    static const QCursor cursor = [] {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath arrow;
        arrow.arcMoveTo(QRectF(5, 5, 14, 14), 120);
        arrow.arcTo(QRectF(5, 5, 14, 14), 120, -260);
        const QPointF tip = arrow.currentPosition();
        QPainterPath head;
        head.moveTo(tip + QPointF(-4.5, -1.0));
        head.lineTo(tip + QPointF(1.5, 0.5));
        head.lineTo(tip + QPointF(-1.5, 5.0));
        const std::pair<QColor, qreal> passes[] = {{QColor(0, 0, 0, 200), 3.5}, {QColor(Qt::white), 1.5}};
        for (const auto& [color, width] : passes) {
            p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(arrow);
            p.drawPath(head);
        }
        p.end();
        return QCursor(pixmap, 12, 12);
    }();
    return cursor;
}
} // namespace

QSizeF ViewerWidget::pictureSize() const { return m_transform.imageRect().size(); }

QPointF ViewerWidget::widgetToPicture(const QPointF& position) const
{
    const QPointF n = widgetToNormalized(position);
    const QSizeF size = pictureSize();
    return {n.x() * size.width(), n.y() * size.height()};
}

QPointF ViewerWidget::pictureToWidget(const QPointF& picture) const
{
    const QSizeF size = pictureSize();
    if (size.isEmpty()) return {};
    return normalizedToWidget(QPointF(picture.x() / size.width(), picture.y() / size.height()));
}

std::optional<annotation::TransformBox> ViewerWidget::selectionBox() const
{
    if (m_stroke && m_stroke->kind == ActiveStroke::Kind::Transform && m_stroke->handle >= 0
        && m_stroke->handle <= annotation::kRotateHandle)
        return m_stroke->currentBox;
    std::optional<annotation::AnnotationItem> item = movePreview();
    if (!item) item = findItem(m_selectedItemId);
    if (!item || !annotation::hasBoxHandles(*item) || pictureSize().isEmpty()) return std::nullopt;
    return handleBox(annotation::transformBox(*item, pictureSize()));
}

QPointF ViewerWidget::rotateKnob(const annotation::TransformBox& box) const
{
    return box.toPicture(QPointF(0.0, -box.size.height() / 2.0 - kRotateKnobOffset));
}

std::optional<QPointF> ViewerWidget::transformHandlePosition(int handle) const
{
    if (m_tool.tool != annotation::AnnotationTool::Select || m_textEdit) return std::nullopt;
    const auto item = findItem(m_selectedItemId);
    if (!item) return std::nullopt;
    if (annotation::hasEndPointHandles(*item)) {
        if (handle != annotation::kStartPointHandle && handle != annotation::kEndPointHandle) return std::nullopt;
        const annotation::StrokePoint& p = item->points[handle == annotation::kStartPointHandle ? 0 : 1];
        return normalizedToWidget(QPointF(p.x, p.y));
    }
    const auto box = selectionBox();
    if (!box) return std::nullopt;
    if (handle == annotation::kRotateHandle) {
        if (!annotation::canRotate(*item)) return std::nullopt;
        return pictureToWidget(rotateKnob(*box));
    }
    if (handle < 0 || handle > 7 || (handle % 2 == 1 && !annotation::hasEdgeHandles(*item))) return std::nullopt;
    return pictureToWidget(box->handlePoint(handle));
}

int ViewerWidget::transformHandleAt(const QPointF& widgetPosition) const
{
    if (!m_annotateMode || m_tool.tool != annotation::AnnotationTool::Select || m_textEdit) return annotation::kNoHandle;
    const auto item = findItem(m_selectedItemId);
    if (!item) return annotation::kNoHandle;
    auto near = [&](int handle) {
        const auto at = transformHandlePosition(handle);
        if (!at) return false;
        const QPointF d = widgetPosition - *at;
        return std::abs(d.x()) <= kTransformHandleReach && std::abs(d.y()) <= kTransformHandleReach;
    };
    // The end a line was drawn to is on top, as it is in the picture.
    for (int handle : {int(annotation::kEndPointHandle), int(annotation::kStartPointHandle),
                       int(annotation::kRotateHandle)})
        if (near(handle)) return handle;
    for (int handle = 0; handle < 8; ++handle)
        if (near(handle)) return handle;
    // Just outside a corner rotates, as in Photoshop.
    if (annotation::canRotate(*item)) {
        const auto box = selectionBox();
        const QPointF p = widgetToPicture(widgetPosition);
        if (box && !box->polygon().containsPoint(p, Qt::OddEvenFill)) {
            for (int corner = 0; corner < 8; corner += 2) {
                const QPointF d = p - box->handlePoint(corner);
                if (std::hypot(d.x(), d.y()) <= kRotateZone) return annotation::kRotateHandle;
            }
        }
    }
    return annotation::kNoHandle;
}

void ViewerWidget::updateSelectCursor(const QPointF& position)
{
    if (!m_annotateMode || m_middlePanning || m_tool.tool != annotation::AnnotationTool::Select) return;
    const int handle = transformHandleAt(position);
    if (handle == annotation::kNoHandle) {
        setCursor(Qt::ArrowCursor);
    } else if (handle == annotation::kRotateHandle) {
        setCursor(rotateCursor());
    } else if (handle == annotation::kStartPointHandle || handle == annotation::kEndPointHandle) {
        setCursor(Qt::CrossCursor);
    } else if (const auto box = selectionBox()) {
        // A resize arrow pointing the way this handle faces on screen, so a
        // turned box still shows the right one.
        const QPointF d = pictureToWidget(box->handlePoint(handle)) - pictureToWidget(box->center);
        const qreal angle = std::fmod(qRadiansToDegrees(std::atan2(d.y(), d.x())) + 360.0, 180.0);
        static const Qt::CursorShape shapes[4] = {Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                                  Qt::SizeBDiagCursor};
        setCursor(shapes[int(std::floor((angle + 22.5) / 45.0)) % 4]);
    }
}

void ViewerWidget::paintSelection(QPainter& painter, const QRectF& target, const annotation::AnnotationItem& item)
{
    // Painter space here is the unflipped picture (the flip is on the painter).
    const QPointF origin = target.topLeft();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    auto outline = [&](const QPolygonF& polygon) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0, 170), 3.0));
        painter.drawPolygon(polygon);
        painter.setPen(QPen(QColor(255, 255, 255, 230), 1.0, Qt::DashLine));
        painter.drawPolygon(polygon);
    };
    auto handle = [&](const QPointF& at, bool round) {
        painter.setPen(QPen(QColor(0, 0, 0, 200), 1.0));
        painter.setBrush(QColor(255, 255, 255, 235));
        const QRectF r(at - QPointF(3.5, 3.5), QSizeF(7.0, 7.0));
        if (round) painter.drawEllipse(r.adjusted(-0.5, -0.5, 0.5, 0.5));
        else painter.drawRect(r);
    };
    const QSizeF size = pictureSize();
    if (annotation::hasEndPointHandles(item)) {
        for (const annotation::StrokePoint& p : item.points)
            handle(origin + QPointF(p.x * size.width(), p.y * size.height()), true);
    } else if (const auto box = selectionBox()) {
        outline(box->polygon().translated(origin));
        if (annotation::canRotate(item)) {
            const QPointF top = origin + box->handlePoint(1);
            const QPointF knob = origin + rotateKnob(*box);
            painter.setPen(QPen(QColor(0, 0, 0, 170), 3.0));
            painter.drawLine(top, knob);
            painter.setPen(QPen(QColor(255, 255, 255, 230), 1.0));
            painter.drawLine(top, knob);
            handle(knob, true);
        }
        // While turning, the box shows the turn without handles; once let go
        // it hugs the result again (a stroke's box is always upright).
        const bool rotating = m_stroke && m_stroke->kind == ActiveStroke::Kind::Transform
            && m_stroke->handle == annotation::kRotateHandle;
        if (!rotating) {
            for (int i = 0; i < 8; ++i)
                if (i % 2 == 0 || annotation::hasEdgeHandles(item)) handle(origin + box->handlePoint(i), false);
        }
    } else {
        outline(QPolygonF(annotation::AnnotationRenderer::itemBounds(target, item).adjusted(-3, -3, 3, 3)));
    }
    painter.restore();
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
    if (!m_stroke || m_stroke->kind != ActiveStroke::Kind::Draw) return std::nullopt;
    annotation::AnnotationItem item = m_stroke->item;
    if (m_stroke->builder) item.points = m_stroke->builder->points();
    return item;
}

std::optional<annotation::AnnotationItem> ViewerWidget::movePreview() const
{
    if (m_stroke && m_stroke->kind == ActiveStroke::Kind::Transform) return m_stroke->transformed;
    if (!m_stroke || m_stroke->kind != ActiveStroke::Kind::Move) return std::nullopt;
    return annotation::translatedItem(m_stroke->item, m_stroke->currentNormalized - m_stroke->pressNormalized);
}

std::optional<annotation::AnnotationItem> ViewerWidget::findItem(quint64 id) const
{
    if (id == 0 || m_annotationFrame.frame != m_frame.frameIndex) return std::nullopt;
    for (const annotation::AnnotationItem& item : m_annotationFrame.items)
        if (item.id == id) return item;
    return std::nullopt;
}

quint64 ViewerWidget::itemAt(const QPointF& widgetPosition, bool textOnly) const
{
    if (m_annotationFrame.isEmpty() || m_annotationFrame.frame != m_frame.frameIndex) return 0;
    const QRectF target = m_transform.imageRect();
    QPointF p = widgetPosition;
    if (m_flipHorizontal) p.setX(target.left() + target.right() - p.x());
    // Topmost first: the last drawn item is on top.
    for (auto it = m_annotationFrame.items.crbegin(); it != m_annotationFrame.items.crend(); ++it) {
        if (textOnly && it->tool != annotation::AnnotationTool::Text) continue;
        if (annotation::AnnotationRenderer::hitTest(target, *it, p, 4.0)) return it->id;
    }
    return 0;
}

bool ViewerWidget::isDoublePress(const QPointF& position, quint64 itemId)
{
    const bool isDouble = itemId != 0 && itemId == m_lastPressItemId && m_lastPressTimer.isValid()
        && m_lastPressTimer.elapsed() <= QApplication::doubleClickInterval()
        && std::hypot(position.x() - m_lastPressPosition.x(), position.y() - m_lastPressPosition.y()) <= 6.0;
    m_lastPressTimer.restart();
    m_lastPressPosition = position;
    m_lastPressItemId = isDouble ? 0 : itemId; // a third press starts over
    return isDouble;
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
    using Kind = ActiveStroke::Kind;
    if (!canAnnotate()) return false;
    // While typing, the frame's handles resize the box and its border moves
    // it; a press anywhere else finishes the text, like clicking away from a
    // text box in a design tool, and does nothing else.
    if (m_textEdit) {
        const int handle = textFrameHandleAt(position);
        if (handle < 0) {
            commitTextEdit();
            return true;
        }
        ActiveStroke frame;
        frame.kind = ActiveStroke::Kind::TextFrame;
        frame.frameIndex = m_textEdit->frameIndex;
        frame.fromTablet = fromTablet;
        frame.handle = handle;
        frame.item = m_textEdit->item;
        frame.pressWidgetPosition = position;
        frame.pressNormalized = unflippedNormalized(position);
        frame.currentNormalized = frame.pressNormalized;
        m_stroke = frame;
        return true;
    }
    const QPointF n = widgetToNormalized(position);
    const bool inside = n.x() >= 0.0 && n.x() <= 1.0 && n.y() >= 0.0 && n.y() <= 1.0;

    ActiveStroke stroke;
    stroke.frameIndex = m_frame.frameIndex;
    stroke.fromTablet = fromTablet;
    stroke.pressWidgetPosition = position;
    stroke.pressNormalized = n;
    stroke.currentNormalized = n;

    if (eraserPointer || m_tool.tool == AnnotationTool::Eraser) {
        stroke.kind = Kind::Erase;
        stroke.lastWidgetPosition = position;
        m_stroke = stroke;
        emit annotationEraseStarted(stroke.frameIndex);
        eraseAlong(position, position, pressure);
        update();
        return true;
    }

    if (m_tool.tool == AnnotationTool::Select) {
        // The selection's handles come first: they can sit over other items.
        if (const auto selected = findItem(m_selectedItemId)) {
            const int handle = transformHandleAt(position);
            if (handle != annotation::kNoHandle) {
                stroke.kind = Kind::Transform;
                stroke.item = *selected;
                stroke.handle = handle;
                if (const auto shown = selectionBox()) stroke.box = *shown; // none for a line's ends
                stroke.currentBox = stroke.box;
                stroke.pressPicture = widgetToPicture(position);
                stroke.transformed = *selected;
                m_stroke = stroke;
                update();
                return true;
            }
        }
        quint64 hit = itemAt(position, false);
        // Inside the selection's box but on no item (the middle of an
        // outline, say) still grabs the selection, as in Photoshop.
        if (hit == 0 && m_selectedItemId != 0) {
            const auto box = selectionBox();
            if (box && box->polygon().containsPoint(widgetToPicture(position), Qt::OddEvenFill))
                hit = m_selectedItemId;
        }
        const auto item = findItem(hit);
        if (isDoublePress(position, hit) && item && item->tool == AnnotationTool::Text) {
            m_selectedItemId = hit;
            beginTextEdit(stroke.frameIndex, *item, true);
            return true;
        }
        m_selectedItemId = hit;
        if (item) {
            stroke.kind = Kind::Move;
            stroke.item = *item;
            m_stroke = stroke;
        }
        update();
        return true;
    }

    if (m_tool.tool == AnnotationTool::Text) {
        // Clicking existing text edits it; anywhere else starts new text.
        if (const auto text = findItem(itemAt(position, true))) {
            beginTextEdit(stroke.frameIndex, *text, true);
            return true;
        }
        if (!inside) return false;
        stroke.kind = Kind::TextBox;
        m_stroke = stroke;
        update();
        return true;
    }

    // Drawing starts on the picture; a press on the letterbox does nothing.
    if (!inside) return false;

    stroke.kind = Kind::Draw;
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
    m_lastStrokeDirty = {};
    update();
    return true;
}

void ViewerWidget::continueStroke(const QPointF& position, float pressure, float tiltX, float tiltY)
{
    using Kind = ActiveStroke::Kind;
    if (!m_stroke) return;
    const QPointF n = widgetToNormalized(position);
    switch (m_stroke->kind) {
    case Kind::Erase:
        eraseAlong(m_stroke->lastWidgetPosition, position, pressure);
        m_stroke->lastWidgetPosition = position;
        break;
    case Kind::Move:
        m_stroke->currentNormalized = n;
        // Hide the original only once it actually moves, so a plain click to
        // select never flickers.
        if (m_hiddenItemId != m_stroke->item.id && n != m_stroke->pressNormalized) {
            m_hiddenItemId = m_stroke->item.id;
            ++m_annotationGeneration;
        }
        break;
    case Kind::Transform: {
        ActiveStroke& s = *m_stroke;
        if (s.handle == annotation::kStartPointHandle || s.handle == annotation::kEndPointHandle) {
            s.transformed = annotation::movedEndPoint(s.item, s.handle, n);
        } else {
            const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
            annotation::TransformOptions options;
            options.keepAspect = (modifiers & Qt::ShiftModifier) || s.item.tool == annotation::AnnotationTool::Text;
            options.fromCenter = modifiers & Qt::AltModifier;
            options.snapAngle = modifiers & Qt::ShiftModifier;
            s.currentBox = annotation::draggedBox(s.box, s.handle, s.pressPicture, widgetToPicture(position), options);
            s.transformed = annotation::transformedItem(s.item, pictureSize(), s.box, s.currentBox);
        }
        // As with a move, the original hides only once something changes.
        if (m_hiddenItemId != s.item.id && s.transformed != s.item) {
            m_hiddenItemId = s.item.id;
            ++m_annotationGeneration;
        }
        break;
    }
    case Kind::TextBox:
        m_stroke->currentNormalized = QPointF(std::clamp(n.x(), 0.0, 1.0), std::clamp(n.y(), 0.0, 1.0));
        break;
    case Kind::TextFrame: {
        if (!m_textEdit) break;
        const QPointF u = unflippedNormalized(position);
        const QPointF delta = u - m_stroke->pressNormalized;
        const annotation::AnnotationItem& start = m_stroke->item;
        annotation::AnnotationItem next = start;
        if (m_stroke->handle == 8 || !annotation::isBoxText(start)) {
            next = annotation::translatedItem(start, delta);
        } else {
            // Resize: move the edges the handle owns, keeping a usable minimum.
            const QRectF target = m_transform.imageRect();
            const double minW = 24.0 / std::max(1.0, target.width());
            const double minH = 12.0 / std::max(1.0, target.height());
            double left = start.points[0].x, top = start.points[0].y;
            double right = start.points[1].x, bottom = start.points[1].y;
            const int h = m_stroke->handle;
            if (h == 0 || h == 6 || h == 7) left = std::clamp(left + delta.x(), 0.0, right - minW);
            if (h == 2 || h == 3 || h == 4) right = std::clamp(right + delta.x(), left + minW, 1.0);
            if (h == 0 || h == 1 || h == 2) top = std::clamp(top + delta.y(), 0.0, bottom - minH);
            if (h == 4 || h == 5 || h == 6) bottom = std::clamp(bottom + delta.y(), top + minH, 1.0);
            next.points[0].x = float(left);
            next.points[0].y = float(top);
            next.points[1].x = float(right);
            next.points[1].y = float(bottom);
        }
        m_textEdit->item.points = next.points;
        positionTextEditor();
        break;
    }
    case Kind::Draw:
        if (m_stroke->builder) {
            m_stroke->builder->addSample(n, pressure, tiltX, tiltY);
            // Repaint only around the newest ink (and where it was last
            // time, so a stabilised tail settles), not the whole picture.
            const QRect dirty = freehandDirtyRect();
            update(dirty | m_lastStrokeDirty);
            m_lastStrokeDirty = dirty;
            return;
        } else if (m_stroke->item.points.size() == 2) {
            m_stroke->item.points[1].x = float(std::clamp(n.x(), 0.0, 1.0));
            m_stroke->item.points[1].y = float(std::clamp(n.y(), 0.0, 1.0));
        }
        break;
    }
    update();
}

void ViewerWidget::finishStroke()
{
    using Kind = ActiveStroke::Kind;
    if (!m_stroke) return;
    ActiveStroke stroke = std::move(*m_stroke);
    m_stroke.reset();
    switch (stroke.kind) {
    case Kind::Erase:
        emit annotationEraseFinished();
        break;
    case Kind::Move: {
        const annotation::AnnotationItem moved =
            annotation::translatedItem(stroke.item, stroke.currentNormalized - stroke.pressNormalized);
        if (moved != stroke.item) emit annotationItemChanged(stroke.frameIndex, moved);
        // Whether or not the owner applied it, show the real data again.
        if (m_hiddenItemId == stroke.item.id) {
            m_hiddenItemId = 0;
            ++m_annotationGeneration;
        }
        break;
    }
    case Kind::Transform:
        if (stroke.transformed != stroke.item && annotation::isValidItem(stroke.transformed))
            emit annotationItemChanged(stroke.frameIndex, stroke.transformed);
        if (m_hiddenItemId == stroke.item.id) {
            m_hiddenItemId = 0;
            ++m_annotationGeneration;
        }
        break;
    case Kind::TextFrame:
        // Keep typing where the reviewer left off.
        if (m_textEditor) m_textEditor->setFocus(Qt::OtherFocusReason);
        break;
    case Kind::TextBox: {
        // A click places point text; a drag defines a box the text wraps in.
        annotation::AnnotationItem item = m_tool.makeItem(sourceImageHeight());
        item.tool = annotation::AnnotationTool::Text;
        const QPointF a = stroke.pressNormalized;
        const QPointF b = stroke.currentNormalized;
        const QPointF wa = normalizedToWidget(a);
        const QPointF wb = normalizedToWidget(b);
        const bool box = std::abs(wb.x() - wa.x()) >= 12.0;
        const float left = float(std::clamp(std::min(a.x(), b.x()), 0.0, 1.0));
        const float top = float(std::clamp(box ? std::min(a.y(), b.y()) : a.y(), 0.0, 1.0));
        item.points = {{box ? left : float(std::clamp(a.x(), 0.0, 1.0)), top, 1.0f, 0.0f, 0.0f}};
        if (box) item.points.append({float(std::clamp(std::max(a.x(), b.x()), 0.0, 1.0)),
                                     float(std::clamp(std::max(a.y(), b.y()), 0.0, 1.0)), 1.0f, 0.0f, 0.0f});
        beginTextEdit(stroke.frameIndex, item, false);
        break;
    }
    case Kind::Draw:
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
        break;
    }
    update();
}

void ViewerWidget::cancelStroke()
{
    if (!m_stroke) return;
    const ActiveStroke stroke = std::move(*m_stroke);
    m_stroke.reset();
    if ((stroke.kind == ActiveStroke::Kind::Move || stroke.kind == ActiveStroke::Kind::Transform)
        && m_hiddenItemId == stroke.item.id) {
        m_hiddenItemId = 0;
        ++m_annotationGeneration;
    }
    // Erasing already applied stays applied; only the undo group is closed.
    if (stroke.isEraser()) emit annotationEraseFinished();
}

void ViewerWidget::eraseAlong(const QPointF& from, const QPointF& to, float pressure)
{
    const qint64 frameIndex = m_stroke ? m_stroke->frameIndex : m_frame.frameIndex;
    if (m_annotationFrame.isEmpty() || m_annotationFrame.frame != frameIndex) return;
    const QSizeF imageSize = displaySourceSize();
    if (imageSize.isEmpty()) return;
    // Radius in image pixels; at least about one screen pixel at any zoom.
    const qreal radius = std::max(m_tool.eraserRadius(pressure), 0.75 / widgetPixelsPerSourcePixel());
    const QPointF a = widgetToNormalized(from);
    const QPointF b = widgetToNormalized(to);
    // Pointer samples can be far apart on a fast drag: place discs along the
    // path at half-radius spacing so the swept band has no gaps.
    const qreal distance = std::hypot((b.x() - a.x()) * imageSize.width(), (b.y() - a.y()) * imageSize.height());
    const int steps = std::clamp(int(std::ceil(distance / (0.5 * radius))), 1, 2048);
    // The start point was covered by the previous call (or the press).
    QVector<QPointF> centers;
    if (from == to) {
        centers = {a};
    } else {
        centers.reserve(steps);
        for (int s = 1; s <= steps; ++s) centers.append(a + (b - a) * (qreal(s) / steps));
    }
    emit annotationEraseRequested(frameIndex, centers, radius, imageSize);
}

// --- In-place text editing -----------------------------------------------

void ViewerWidget::beginTextEdit(qint64 frameIndex, const annotation::AnnotationItem& item, bool existing)
{
    finishTextEdit(true);
    m_textEdit = TextEditState{frameIndex, item, existing};
    if (existing) {
        // The editor shows the text while it is edited; hide the drawn copy.
        m_hiddenItemId = item.id;
        ++m_annotationGeneration;
    }

    auto* editor = new QTextEdit(this);
    editor->setObjectName(QStringLiteral("annotationTextEditor"));
    editor->setAcceptRichText(false);
    editor->setFrameShape(QFrame::NoFrame);
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->document()->setDocumentMargin(0.0);
    editor->viewport()->setAutoFillBackground(false);
    editor->setAutoFillBackground(false);
    if (annotation::isBoxText(item)) {
        editor->setLineWrapMode(QTextEdit::FixedPixelWidth);
        editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    } else {
        editor->setLineWrapMode(QTextEdit::NoWrap);
    }
    editor->setPlainText(item.text);
    editor->moveCursor(QTextCursor::End);
    editor->installEventFilter(this);
    connect(editor, &QTextEdit::textChanged, this, [this] {
        positionTextEditor();
        update(); // the dashed frame follows the text
    });
    m_textEditor = editor;
    applyTextEditStyle();
    positionTextEditor();
    editor->show();
    editor->setFocus(Qt::OtherFocusReason);
    update();
}

void ViewerWidget::applyTextEditStyle()
{
    if (!m_textEdit || !m_textEditor) return;
    const annotation::AnnotationItem& item = m_textEdit->item;
    const QFont font = annotation::AnnotationRenderer::textFont(m_transform.imageRect(), item);
    QColor color = item.color.isValid() ? item.color : QColor(Qt::white);
    color.setAlphaF(std::clamp(item.opacity, 0.05, 1.0));
    // The application stylesheet sets a pixel font size on every widget, which
    // would override setFont(); the editor's own stylesheet must carry the
    // size so typed text is shown at exactly the size it will be drawn.
    m_textEditor->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; border: none; padding: 0px; margin: 0px;"
        " font-family: \"%1\"; font-size: %2px; font-weight: %3; color: %4; }")
        .arg(font.family())
        .arg(font.pixelSize())
        .arg(font.bold() ? QStringLiteral("bold") : QStringLiteral("normal"))
        .arg(color.name(QColor::HexArgb)));
    m_textEditor->setFont(font);
    m_textEditor->document()->setDefaultFont(font);
}

void ViewerWidget::positionTextEditor()
{
    if (!m_textEdit || !m_textEditor) return;
    const QRectF target = m_transform.imageRect();
    const annotation::AnnotationItem& item = m_textEdit->item;
    const QFont font = annotation::AnnotationRenderer::textFont(target, item);
    if (m_textEditor->font().pixelSize() != font.pixelSize()) applyTextEditStyle(); // zoom changed
    // Placed unmirrored even when the view is flipped, so it stays readable.
    const QPointF anchor(target.left() + item.points[0].x * target.width(),
                         target.top() + item.points[0].y * target.height());
    QTextDocument* document = m_textEditor->document();
    qreal width = 0.0;
    qreal minimumHeight = QFontMetricsF(font).lineSpacing();
    if (annotation::isBoxText(item)) {
        width = std::max<qreal>(16.0, (item.points[1].x - item.points[0].x) * target.width());
        minimumHeight = std::max(minimumHeight, qreal(item.points[1].y - item.points[0].y) * target.height());
        m_textEditor->setLineWrapColumnOrWidth(int(std::ceil(width)));
        document->setTextWidth(width);
    } else {
        document->setTextWidth(-1);
        width = document->idealWidth() + QFontMetricsF(font).averageCharWidth() * 2.0;
    }
    const qreal height = std::max<qreal>(document->size().height(), minimumHeight);
    m_textEditor->setGeometry(QRectF(anchor, QSizeF(width + 2.0, height + 2.0)).toAlignedRect());
    update();
}

QPointF ViewerWidget::unflippedNormalized(const QPointF& position) const
{
    const QRectF target = m_transform.imageRect();
    if (target.width() <= 0.0 || target.height() <= 0.0) return {};
    return {(position.x() - target.left()) / target.width(), (position.y() - target.top()) / target.height()};
}

namespace {
constexpr qreal kTextFrameMargin = 6.0; // frame sits this far outside the text field
constexpr qreal kHandleSize = 8.0;
}

QRectF ViewerWidget::textEditFrame() const
{
    if (!m_textEdit || !m_textEditor) return {};
    return QRectF(m_textEditor->geometry())
        .adjusted(-kTextFrameMargin, -kTextFrameMargin, kTextFrameMargin, kTextFrameMargin);
}

int ViewerWidget::textFrameHandleAt(const QPointF& position) const
{
    const QRectF frame = textEditFrame();
    if (frame.isEmpty()) return -1;
    if (annotation::isBoxText(m_textEdit->item)) {
        const QPointF points[8] = {frame.topLeft(),     QPointF(frame.center().x(), frame.top()),
                                   frame.topRight(),    QPointF(frame.right(), frame.center().y()),
                                   frame.bottomRight(), QPointF(frame.center().x(), frame.bottom()),
                                   frame.bottomLeft(),  QPointF(frame.left(), frame.center().y())};
        for (int i = 0; i < 8; ++i) {
            const QPointF d = position - points[i];
            if (std::abs(d.x()) <= kHandleSize && std::abs(d.y()) <= kHandleSize) return i;
        }
    }
    // The band between the frame and the text field moves the text.
    const QRectF outer = frame.adjusted(-2, -2, 2, 2);
    const QRectF inner = QRectF(m_textEditor->geometry());
    if (outer.contains(position) && !inner.contains(position)) return 8;
    return -1;
}

void ViewerWidget::commitTextEdit() { finishTextEdit(true); }
void ViewerWidget::cancelTextEdit() { finishTextEdit(false); }

void ViewerWidget::finishTextEdit(bool commit)
{
    if (!m_textEdit) return;
    const TextEditState state = std::move(*m_textEdit);
    m_textEdit.reset();
    QString text;
    if (m_textEditor) {
        text = m_textEditor->toPlainText();
        m_textEditor->removeEventFilter(this);
        m_textEditor->hide();
        m_textEditor->deleteLater();
        m_textEditor = nullptr;
    }
    if (state.existing && m_hiddenItemId == state.item.id) {
        m_hiddenItemId = 0;
        ++m_annotationGeneration;
    }
    // Trailing blank lines are a slip of the Enter key, not content.
    while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char(' '))) text.chop(1);
    if (commit) {
        annotation::AnnotationItem item = state.item;
        item.text = text.left(annotation::AnnotationLimits::kMaxTextLength);
        if (text.trimmed().isEmpty()) {
            // Emptying existing text deletes it; empty new text is simply dropped.
            if (state.existing) emit annotationItemDeleteRequested(state.frameIndex, state.item.id);
        } else if (state.existing) {
            if (item != state.item) emit annotationItemChanged(state.frameIndex, item);
        } else if (annotation::isValidItem(item)) {
            emit annotationItemDrawn(state.frameIndex, item);
        }
    }
    if (m_annotateMode) setFocus(Qt::OtherFocusReason);
    update();
}

bool ViewerWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (m_textEditor && watched == m_textEditor) {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                cancelTextEdit();
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && (key->modifiers() & Qt::ControlModifier)) {
                commitTextEdit();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Clicking anywhere else (another panel, the timeline) finishes the text.
            const auto reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason)
                QTimer::singleShot(0, this, [this] { commitTextEdit(); });
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ViewerWidget::keyPressEvent(QKeyEvent* event)
{
    // Escape mid-drag puts the selection back where it was.
    if (m_stroke && event->key() == Qt::Key_Escape
        && (m_stroke->kind == ActiveStroke::Kind::Move || m_stroke->kind == ActiveStroke::Kind::Transform)) {
        cancelStroke();
        updateAnnotateCursor();
        update();
        event->accept();
        return;
    }
    if (m_annotateMode && m_selectedItemId != 0 && findItem(m_selectedItemId)
        && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        const quint64 id = m_selectedItemId;
        m_selectedItemId = 0;
        emit annotationItemDeleteRequested(m_frame.frameIndex, id);
        event->accept();
        return;
    }
    if (m_annotateMode && m_selectedItemId != 0 && event->key() == Qt::Key_Escape) {
        clearSelection();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ViewerWidget::updateAnnotateCursor()
{
    if (m_middlePanning) return;
    if (!m_annotateMode) {
        unsetCursor();
        return;
    }
    using annotation::AnnotationTool;
    if (m_tool.tool == AnnotationTool::Select) {
        setCursor(Qt::ArrowCursor);
    } else if (m_tool.tool == AnnotationTool::Text) {
        setCursor(Qt::IBeamCursor);
    } else {
        const bool brush = annotation::isFreehandTool(m_tool.tool) || m_tool.tool == AnnotationTool::Eraser;
        // Freehand tools draw their own size circle, so the arrow would only hide it.
        setCursor(brush ? Qt::BlankCursor : Qt::CrossCursor);
    }
}

void ViewerWidget::setHoverPosition(std::optional<QPointF> position)
{
    if (m_hoverPosition == position) return;
    const std::optional<QPointF> previous = m_hoverPosition;
    m_hoverPosition = position;
    if (!m_annotateMode) return;
    // Only the cursor's old and new spots need repainting.
    QRect dirty;
    if (previous) dirty |= brushCursorRect(*previous);
    if (position) dirty |= brushCursorRect(*position);
    if (!dirty.isEmpty()) update(dirty);
}

QRect ViewerWidget::brushCursorRect(const QPointF& position) const
{
    const qreal imagePixels = std::max(m_tool.eraserRadius(1.0f), 0.5 * m_tool.sizePixels);
    const qreal radius = std::max<qreal>(1.5, imagePixels * widgetPixelsPerSourcePixel()) + 4.0;
    return QRectF(position - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)).toAlignedRect();
}

QRect ViewerWidget::freehandDirtyRect() const
{
    if (!m_stroke || m_stroke->kind != ActiveStroke::Kind::Draw || !m_stroke->builder) return {};
    const QVector<annotation::StrokePoint>& points = m_stroke->builder->points();
    if (points.isEmpty()) return {};
    // The smoothed curve can still bend over the last few samples.
    const int first = std::max(0, int(points.size()) - 6);
    QRectF bounds;
    for (int i = first; i < points.size(); ++i) {
        const QPointF p = normalizedToWidget(QPointF(points[i].x, points[i].y));
        bounds = bounds.isNull() ? QRectF(p, QSizeF(0.01, 0.01)) : bounds.united(QRectF(p, QSizeF(0.01, 0.01)));
    }
    // Generous: full brush diameter (stamp tips can be wider than round ones).
    const qreal pad = std::max<qreal>(2.0, m_tool.sizePixels * widgetPixelsPerSourcePixel()) * 1.5 + 4.0;
    return bounds.adjusted(-pad, -pad, pad, pad).toAlignedRect();
}

void ViewerWidget::paintCachedPicture(QPainter& painter, const QRectF& target, const QColor& background)
{
    const qreal dpr = devicePixelRatioF();
    const QSize pixelSize = (QSizeF(size()) * dpr).toSize();
    const bool smooth = m_transform.zoomRatio() <= 1.0;
    if (m_pictureCache.size() != pixelSize || !qFuzzyCompare(m_pictureCache.devicePixelRatio(), dpr)
        || m_pictureCacheKey != m_frame.image.cacheKey() || m_pictureCacheTarget != target
        || m_pictureCacheFlipped != m_flipHorizontal || m_pictureCacheTrace != m_tracingPaperPercent
        || m_pictureCacheSmooth != smooth || m_pictureCacheBackground != background.rgba()) {
        m_pictureCache = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
        m_pictureCache.setDevicePixelRatio(dpr);
        m_pictureCache.fill(background);
        QPainter cache(&m_pictureCache);
        cache.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        if (m_flipHorizontal) {
            cache.save();
            cache.translate(target.left() + target.right(), 0.0);
            cache.scale(-1.0, 1.0);
        }
        cache.drawImage(target, m_frame.image);
        if (m_flipHorizontal) cache.restore();
        // Tracing paper sits over the picture and under the drawings.
        if (m_tracingPaperPercent > 0)
            cache.fillRect(target, QColor(255, 255, 255, m_tracingPaperPercent * 255 / 100));
        cache.end();
        m_pictureCacheKey = m_frame.image.cacheKey();
        m_pictureCacheTarget = target;
        m_pictureCacheFlipped = m_flipHorizontal;
        m_pictureCacheTrace = m_tracingPaperPercent;
        m_pictureCacheSmooth = smooth;
        m_pictureCacheBackground = background.rgba();
    }
    painter.drawImage(QPointF(0.0, 0.0), m_pictureCache);
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
    const qreal dpr = devicePixelRatioF();
    const QSize pixelSize = (QSizeF(size()) * dpr).toSize();
    // Ghosts of nearby frames' drawings, under this frame's own.
    if (!m_ghosts.isEmpty() && m_ghostCenterFrame == m_frame.frameIndex) {
        if (m_ghostCacheGeneration != m_ghostGeneration || m_ghostCacheTarget != target
            || m_ghostCacheFlipped != m_flipHorizontal || m_ghostCache.size() != pixelSize
            || !qFuzzyCompare(m_ghostCache.devicePixelRatio(), dpr)) {
            m_ghostCache = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
            m_ghostCache.setDevicePixelRatio(dpr);
            m_ghostCache.fill(Qt::transparent);
            QImage layer(pixelSize, QImage::Format_ARGB32_Premultiplied);
            layer.setDevicePixelRatio(dpr);
            QPainter ghostPainter(&m_ghostCache);
            for (const annotation::GhostFrame& ghost : m_ghosts) {
                // Each ghost is flattened first, so overlapping strokes of one
                // frame fade together instead of building up.
                layer.fill(Qt::transparent);
                QPainter layerPainter(&layer);
                applyFlip(layerPainter);
                for (const annotation::AnnotationItem& item : ghost.frame.items)
                    annotation::AnnotationRenderer::paintItem(layerPainter, target, item);
                if (ghost.tint.isValid()) {
                    layerPainter.resetTransform();
                    layerPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    layerPainter.fillRect(QRectF(QPointF(0, 0), QSizeF(size())), ghost.tint);
                }
                layerPainter.end();
                ghostPainter.setOpacity(ghost.opacity);
                ghostPainter.drawImage(QPointF(0.0, 0.0), layer);
            }
            ghostPainter.end();
            m_ghostCacheGeneration = m_ghostGeneration;
            m_ghostCacheTarget = target;
            m_ghostCacheFlipped = m_flipHorizontal;
        }
        painter.drawImage(QPointF(0.0, 0.0), m_ghostCache);
    }
    // Only draw annotations that belong to the frame on screen.
    if (!m_annotationFrame.isEmpty() && m_annotationFrame.frame == m_frame.frameIndex) {
        const auto& items = m_annotationFrame.items;
        if (m_annotationCacheGeneration != m_annotationGeneration || m_annotationCacheTarget != target
            || m_annotationCacheFlipped != m_flipHorizontal || m_annotationCache.size() != pixelSize
            || !qFuzzyCompare(m_annotationCache.devicePixelRatio(), dpr) || m_annotationCacheItems > items.size()) {
            m_annotationCache = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
            m_annotationCache.setDevicePixelRatio(dpr);
            m_annotationCache.fill(Qt::transparent);
            m_annotationCacheItems = 0;
            m_annotationCacheGeneration = m_annotationGeneration;
            m_annotationCacheTarget = target;
            m_annotationCacheFlipped = m_flipHorizontal;
            ++m_annotationCacheRebuilds;
        }
        if (m_annotationCacheItems < items.size()) {
            // Items are painted in order, so painting only the ones added
            // since the last paint gives exactly the pixels a full rebuild would.
            QPainter cachePainter(&m_annotationCache);
            applyFlip(cachePainter);
            for (qsizetype i = m_annotationCacheItems; i < items.size(); ++i)
                if (items[i].id != m_hiddenItemId)
                    annotation::AnnotationRenderer::paintItem(cachePainter, target, items[i]);
            m_annotationCacheItems = int(items.size());
        }
        painter.drawImage(QPointF(0.0, 0.0), m_annotationCache);
    }
    painter.save();
    applyFlip(painter);
    if (const auto preview = strokePreview(); preview && annotation::isValidItem(*preview))
        annotation::AnnotationRenderer::paintItem(painter, target, *preview);
    const auto moving = movePreview();
    if (moving && m_hiddenItemId == moving->id) annotation::AnnotationRenderer::paintItem(painter, target, *moving);

    // Selection frame, the text box being dragged out, and the frame around
    // text being typed: dashed, light over dark, readable on any picture.
    auto dashed = [&](const QRectF& rect) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0, 170), 1.0));
        painter.drawRect(rect.adjusted(-1, -1, 1, 1));
        QPen light(QColor(255, 255, 255, 230), 1.0, Qt::DashLine);
        painter.setPen(light);
        painter.drawRect(rect);
    };
    std::optional<annotation::AnnotationItem> selected = moving ? moving : findItem(m_selectedItemId);
    if (selected && m_tool.tool == annotation::AnnotationTool::Select && !m_textEdit)
        paintSelection(painter, target, *selected);
    if (m_stroke && m_stroke->kind == ActiveStroke::Kind::TextBox) {
        const QPointF a(target.left() + m_stroke->pressNormalized.x() * target.width(),
                        target.top() + m_stroke->pressNormalized.y() * target.height());
        const QPointF b(target.left() + m_stroke->currentNormalized.x() * target.width(),
                        target.top() + m_stroke->currentNormalized.y() * target.height());
        dashed(QRectF(a, b).normalized());
    }
    painter.restore();
    if (m_textEditor && m_textEditor->isVisible()) {
        const QRectF frame = textEditFrame();
        dashed(frame);
        if (annotation::isBoxText(m_textEdit->item)) {
            // Resize handles: the box can be sized before or while typing.
            const QPointF handles[8] = {frame.topLeft(),     QPointF(frame.center().x(), frame.top()),
                                        frame.topRight(),    QPointF(frame.right(), frame.center().y()),
                                        frame.bottomRight(), QPointF(frame.center().x(), frame.bottom()),
                                        frame.bottomLeft(),  QPointF(frame.left(), frame.center().y())};
            painter.setPen(QPen(QColor(0, 0, 0, 200), 1.0));
            painter.setBrush(QColor(255, 255, 255, 235));
            for (const QPointF& h : handles)
                painter.drawRect(QRectF(h - QPointF(3.5, 3.5), QSizeF(7.0, 7.0)));
        }
    }
}

void ViewerWidget::paintBrushCursor(QPainter& painter)
{
    using annotation::AnnotationTool;
    if (!m_annotateMode || !m_hoverPosition || m_middlePanning) return;
    const bool erasing = m_hoverIsEraser || m_tool.tool == AnnotationTool::Eraser || (m_stroke && m_stroke->isEraser());
    if (!annotation::isFreehandTool(m_tool.tool) && !erasing) return;
    const qreal imagePixels = erasing ? m_tool.eraserRadius(1.0f) : 0.5 * m_tool.sizePixels;
    const qreal radius = std::max<qreal>(1.5, imagePixels * widgetPixelsPerSourcePixel());
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
