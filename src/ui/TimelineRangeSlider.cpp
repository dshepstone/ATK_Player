#include "ui/TimelineRangeSlider.h"

#include "timeline/TimelineModel.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace atk::ui {
namespace {
constexpr int kInset = 52;
constexpr int kHandleHitWidth = 8;
}

TimelineRangeSlider::TimelineRangeSlider(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::SizeHorCursor);
}

void TimelineRangeSlider::setModel(timeline::TimelineModel* model)
{
    if (m_model == model) return;
    if (m_model) m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        connect(m_model, &timeline::TimelineModel::viewportChanged, this,
                [this] { update(); });
        connect(m_model, &timeline::TimelineModel::frameCountChanged, this,
                [this] { update(); });
    }
    update();
}

QRect TimelineRangeSlider::grooveRect() const
{
    return rect().adjusted(kInset, 6, -kInset, -6);
}

int TimelineRangeSlider::positionForSourceFrame(int64_t frame) const
{
    const QRect groove = grooveRect();
    const int spanPx = std::max(0, groove.width() - 1);
    if (!m_model || m_model->lastFrame() <= 0 || spanPx <= 0) return groove.left();
    const double t = double(std::clamp<int64_t>(frame, 0, m_model->lastFrame()))
                   / double(m_model->lastFrame());
    return groove.left() + static_cast<int>(std::llround(t * spanPx));
}

int64_t TimelineRangeSlider::sourceFrameAtPosition(int x) const
{
    const QRect groove = grooveRect();
    const int spanPx = std::max(0, groove.width() - 1);
    if (!m_model || m_model->lastFrame() <= 0 || spanPx <= 0) return 0;
    const double t = double(std::clamp(x, groove.left(), groove.right()) - groove.left())
                   / double(spanPx);
    return static_cast<int64_t>(std::llround(t * m_model->lastFrame()));
}

QRect TimelineRangeSlider::selectionRect() const
{
    const QRect groove = grooveRect();
    if (!m_model || m_model->frameCount() <= 0) return QRect(groove.left(), groove.top(), 0, groove.height());
    const int left = positionForSourceFrame(m_model->viewport().startFrame());
    const int right = positionForSourceFrame(m_model->viewport().endFrame());
    return QRect(left, groove.top(), std::max(1, right - left + 1), groove.height());
}

void TimelineRangeSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), theme::panelBackground());
    const QRect groove = grooveRect();
    p.fillRect(groove, theme::timelineTrack());
    p.setPen(theme::panelBorder());
    p.drawRect(groove.adjusted(0, 0, -1, -1));
    const QRect selected = selectionRect();
    p.fillRect(selected, theme::timelineRange());
    p.setPen(theme::accent());
    p.drawLine(selected.left(), selected.top(), selected.left(), selected.bottom());
    p.drawLine(selected.right(), selected.top(), selected.right(), selected.bottom());
}

void TimelineRangeSlider::mousePressEvent(QMouseEvent* event)
{
    if (!m_model || event->button() != Qt::LeftButton || m_model->frameCount() <= 0) return;
    const QRect selected = selectionRect();
    const int x = event->position().toPoint().x();
    if (std::abs(x - selected.left()) <= kHandleHitWidth) m_dragMode = DragMode::Left;
    else if (std::abs(x - selected.right()) <= kHandleHitWidth) m_dragMode = DragMode::Right;
    else if (selected.contains(x, selected.center().y())) m_dragMode = DragMode::Body;
    else return;
    m_pressX = x;
    m_pressStart = m_model->viewport().startFrame();
    m_pressEnd = m_model->viewport().endFrame();
}

void TimelineRangeSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_model || m_dragMode == DragMode::None) return;
    const int64_t atPress = sourceFrameAtPosition(m_pressX);
    const int64_t atNow = sourceFrameAtPosition(event->position().toPoint().x());
    const int64_t delta = atNow - atPress;
    if (m_dragMode == DragMode::Left) {
        const int64_t start = std::clamp<int64_t>(m_pressStart + delta, 0,
            m_pressEnd - timeline::TimelineViewport::kMinimumVisibleFrames + 1);
        m_model->setViewportRange(start, m_pressEnd);
    } else if (m_dragMode == DragMode::Right) {
        const int64_t end = std::clamp<int64_t>(m_pressEnd + delta,
            m_pressStart + timeline::TimelineViewport::kMinimumVisibleFrames - 1,
            m_model->lastFrame());
        m_model->setViewportRange(m_pressStart, end);
    }
    else
        m_model->panViewport((m_pressStart + delta) - m_model->viewport().startFrame());
}

void TimelineRangeSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_dragMode = DragMode::None;
}

} // namespace atk::ui
