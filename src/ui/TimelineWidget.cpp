#include "ui/TimelineWidget.h"

#include "timeline/TimelineModel.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cstdlib>

namespace atk::ui {
namespace {

/// Horizontal room reserved at each end for the first/last frame labels.
constexpr int kLabelMargin = 52;
/// Vertical inset of the track inside the widget.
constexpr int kTrackInsetTop = 20;
constexpr int kTrackInsetBottom = 18;
constexpr int kPlayheadHandleWidth = 9;
constexpr int kBookmarkMarkerWidth = 3;

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
    setFocusPolicy(Qt::ClickFocus);
}

TimelineWidget::~TimelineWidget() = default;

void TimelineWidget::setModel(timeline::TimelineModel* model)
{
    if (m_model == model) {
        return;
    }

    if (m_model != nullptr) {
        m_model->disconnect(this);
    }

    m_model = model;

    if (m_model != nullptr) {
        const auto repaint = [this] { update(); };
        connect(m_model, &timeline::TimelineModel::currentFrameChanged, this, repaint);
        connect(m_model, &timeline::TimelineModel::frameCountChanged, this, repaint);
        connect(m_model, &timeline::TimelineModel::playbackRangeChanged, this, repaint);
        connect(m_model, &timeline::TimelineModel::bookmarksChanged, this, repaint);
    }

    update();
}

QSize TimelineWidget::sizeHint() const
{
    return { 800, 64 };
}

QSize TimelineWidget::minimumSizeHint() const
{
    return { 240, 64 };
}

int64_t TimelineWidget::lastFrame() const
{
    if (m_model == nullptr || m_model->frameCount() <= 0) {
        return 0;
    }
    return m_model->frameCount() - 1;
}

QRect TimelineWidget::trackRect() const
{
    return rect().adjusted(kLabelMargin, kTrackInsetTop, -kLabelMargin, -kTrackInsetBottom);
}

int TimelineWidget::xForFrame(int64_t frame) const
{
    const QRect track = trackRect();
    const int64_t last = lastFrame();
    if (last <= 0 || track.width() <= 0) {
        return track.left();
    }
    const double t = static_cast<double>(std::clamp<int64_t>(frame, 0, last))
                   / static_cast<double>(last);
    return track.left() + static_cast<int>(t * track.width());
}

int64_t TimelineWidget::frameForX(int x) const
{
    const QRect track = trackRect();
    const int64_t last = lastFrame();
    if (last <= 0 || track.width() <= 0) {
        return 0;
    }
    const double t = static_cast<double>(std::clamp(x, track.left(), track.right()) - track.left())
                   / static_cast<double>(track.width());
    return static_cast<int64_t>(t * static_cast<double>(last) + 0.5);
}

void TimelineWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(event->rect(), theme::panelBackground());

    paintTrack(painter);
    paintRange(painter);
    paintBookmarks(painter);
    paintFrameLabels(painter);
    paintPlayhead(painter);
}

void TimelineWidget::paintTrack(QPainter& painter)
{
    const QRect track = trackRect();
    if (!track.isValid()) {
        return;
    }

    painter.fillRect(track, theme::timelineTrack());
    painter.setPen(theme::panelBorder());
    painter.drawRect(track.adjusted(0, 0, -1, -1));

    // Tick marks. The spacing adapts so a long clip does not turn the ruler
    // into a solid block.
    const int64_t last = lastFrame();
    if (last <= 0) {
        return;
    }

    const int desiredSpacingPx = 64;
    const int tickCount = std::max(1, track.width() / desiredSpacingPx);
    painter.setPen(theme::tickMark());
    for (int i = 1; i < tickCount; ++i) {
        const int x = track.left() + (track.width() * i) / tickCount;
        painter.drawLine(x, track.bottom() - 5, x, track.bottom() - 1);
    }
}

void TimelineWidget::paintRange(QPainter& painter)
{
    if (m_model == nullptr) {
        return;
    }
    const timeline::PlaybackRange& range = m_model->playbackRange();
    if (!range.enabled || !range.isValid() || lastFrame() <= 0) {
        return;
    }

    const QRect track = trackRect();
    const int left = xForFrame(range.startFrame);
    const int right = xForFrame(range.endFrame);
    const QRect fill(left, track.top() + 1, std::max(1, right - left), track.height() - 2);
    painter.fillRect(fill, theme::timelineRange());

    // In/out ticks at the boundaries.
    painter.setPen(theme::accent());
    painter.drawLine(left, track.top(), left, track.bottom());
    painter.drawLine(right, track.top(), right, track.bottom());
}

void TimelineWidget::paintBookmarks(QPainter& painter)
{
    if (m_model == nullptr || lastFrame() <= 0) {
        return;
    }

    const QRect track = trackRect();
    for (const timeline::Bookmark& bookmark : m_model->bookmarks()) {
        const int x = xForFrame(bookmark.frame);
        const QColor color = bookmark.hasColor() ? timeline::bookmarkColor(bookmark.colorIndex)
                                                 : theme::accent();
        painter.fillRect(QRect(x - kBookmarkMarkerWidth / 2, track.top() - 6,
                               kBookmarkMarkerWidth, 6),
                         color);
    }
}

void TimelineWidget::paintPlayhead(QPainter& painter)
{
    const QRect track = trackRect();
    const int64_t frame = m_model != nullptr ? m_model->currentFrame() : 0;
    const int x = xForFrame(frame);

    painter.setPen(theme::playhead());
    painter.drawLine(x, track.top() - 3, x, track.bottom() + 3);

    // Handle below the track, so it stays grabbable on an empty timeline.
    const QRect handle(x - kPlayheadHandleWidth / 2, track.bottom() + 2,
                       kPlayheadHandleWidth, 5);
    painter.fillRect(handle, theme::playhead());
}

void TimelineWidget::paintFrameLabels(QPainter& painter)
{
    const QRect track = trackRect();
    const int64_t last = lastFrame();

    painter.setPen(theme::textSecondary());

    // First frame, left of the track.
    painter.drawText(QRect(0, track.top(), kLabelMargin - 6, track.height()),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(0));

    // Last frame, right of the track.
    painter.drawText(QRect(track.right() + 6, track.top(), kLabelMargin - 6, track.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString::number(last));
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_scrubbing = true;
    emit seekRequested(frameForX(event->position().toPoint().x()));
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_scrubbing) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    emit seekRequested(frameForX(event->position().toPoint().x()));
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_scrubbing = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_model == nullptr || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    // Snap to a bookmark when the click lands near its marker.
    const int clickX = event->position().toPoint().x();
    for (const timeline::Bookmark& bookmark : m_model->bookmarks()) {
        if (std::abs(xForFrame(bookmark.frame) - clickX) <= 4) {
            emit bookmarkActivated(bookmark.frame);
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace atk::ui
