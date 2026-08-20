#include "ui/TimelineWidget.h"

#include "media/WaveformData.h"

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
///
/// The waveform sits above the track in its own band. Keeping them separate --
/// rather than drawing the waveform inside the track -- means the playhead
/// crosses both, so a dialogue peak and the frame it belongs to line up
/// vertically and can be read at a glance.
constexpr int kWaveformHeight = 46;
constexpr int kWaveformInsetTop = 6;
constexpr int kTrackInsetTop = 20 + kWaveformHeight;
constexpr int kTrackInsetBottom = 18;
constexpr int kPlayheadHandleWidth = 9;
constexpr int kBookmarkMarkerWidth = 3;

/// Minimum gap between preview *decode* requests while dragging.
///
/// This paces the decoder only. The playhead itself is redrawn on every mouse
/// move, so the throttle is invisible to the user's sense of responsiveness --
/// it only governs how often a new picture is asked for. About 30 requests a
/// second, which a decoder can usually sustain; the exact target is always
/// issued on release.
///
/// Minimum gap between preview seeks while dragging the playhead.
///
/// Dragging produces mouse moves far faster than a seek can be decoded. Without
/// pacing, the decoder would spend the whole drag servicing positions the user
/// has already left. The exact target is always issued on release, so the final
/// position is never a throttled approximation.
constexpr qint64 kScrubThrottleMs = 33;

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

        connect(m_model, &timeline::TimelineModel::currentFrameChanged,
                this, [this](int64_t frame) {
                    // Once the decoder has caught up with where the pointer
                    // asked to be, the scrub override has done its job and the
                    // playhead goes back to following the model. Clearing it any
                    // earlier would snap the playhead back to the last decoded
                    // position while the final seek was still in flight.
                    if (m_scrubFrame >= 0 && !m_scrubbing && frame == m_scrubFrame) {
                        m_scrubFrame = -1;
                    }
                    update();
                });

        connect(m_model, &timeline::TimelineModel::frameCountChanged, this, repaint);
        connect(m_model, &timeline::TimelineModel::playbackRangeChanged, this, repaint);
        connect(m_model, &timeline::TimelineModel::bookmarksChanged, this, repaint);
    }
}

QSize TimelineWidget::sizeHint() const
{
    return { 800, 64 + kWaveformHeight };
}

QSize TimelineWidget::minimumSizeHint() const
{
    return { 240, 64 + kWaveformHeight };
}

void TimelineWidget::setWaveform(const media::WaveformData* waveform)
{
    m_waveform = waveform;
    update();
}

void TimelineWidget::setMediaDuration(int64_t durationUs)
{
    if (m_mediaDurationUs == durationUs) {
        return;
    }
    m_mediaDurationUs = durationUs;
    update();
}

void TimelineWidget::refreshWaveform()
{
    // Only the waveform band changed, so the rest of the widget is left alone.
    update(waveformRect());
}

QRect TimelineWidget::waveformRect() const
{
    return QRect(rect().left() + kLabelMargin, rect().top() + kWaveformInsetTop,
                 std::max(0, rect().width() - 2 * kLabelMargin), kWaveformHeight);
}

int64_t TimelineWidget::mediaTimeForX(int x) const
{
    const QRect track = trackRect();
    if (track.width() <= 0 || m_mediaDurationUs <= 0) {
        return 0;
    }
    const double fraction =
        std::clamp(double(x - track.left()) / double(track.width()), 0.0, 1.0);
    return static_cast<int64_t>(fraction * double(m_mediaDurationUs));
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

    paintWaveform(painter);
    paintTrack(painter);
    paintRange(painter);
    paintBookmarks(painter);
    paintFrameLabels(painter);
    paintPlayhead(painter);
}

void TimelineWidget::paintWaveform(QPainter& painter)
{
    const QRect band = waveformRect();
    if (!band.isValid() || band.width() <= 0) {
        return;
    }

    // Media without audio simply has no waveform. Drawing nothing is the
    // correct outcome, not an error worth reporting.
    if (m_waveform == nullptr || m_waveform->isEmpty() || m_mediaDurationUs <= 0) {
        return;
    }

    const int centreY = band.center().y();
    const int halfHeight = band.height() / 2 - 2;
    if (halfHeight <= 0) {
        return;
    }

    // One column per pixel, and a pyramid level whose buckets are about that
    // wide. Reading the finest level for a long file would mean iterating
    // thousands of buckets per column on every repaint -- including every
    // playhead move during playback.
    const int64_t usPerPixel = std::max<int64_t>(1, m_mediaDurationUs / band.width());
    const int level = m_waveform->levelForBucketDuration(usPerPixel);

    const int64_t covered = m_waveform->coveredUs();

    // Saved and restored around the loop: paintTrack() draws with drawRect(),
    // which uses whatever brush is current, so leaving one set here silently
    // repaints the track in the waveform colour.
    painter.save();
    painter.setPen(Qt::NoPen);

    for (int x = band.left(); x <= band.right(); ++x) {
        const int64_t startUs = mediaTimeForX(x);
        const int64_t endUs = mediaTimeForX(x + 1);

        // Analysis fills in left to right, so the tail is simply not drawn yet
        // rather than being drawn as silence.
        if (startUs >= covered) {
            break;
        }

        const media::WaveformPeak peak =
            m_waveform->peakOverRange(level, startUs, std::max(endUs, startUs + 1));
        if (peak.isSilent()) {
            // A one-pixel line keeps silence legible as "audio here, quiet"
            // rather than looking identical to "no data".
            painter.fillRect(QRect(x, centreY, 1, 1), theme::waveformSilence());
            continue;
        }

        const int up = std::clamp(int(peak.maximum * halfHeight), 0, halfHeight);
        const int down = std::clamp(int(-peak.minimum * halfHeight), 0, halfHeight);
        const int top = centreY - up;
        const int height = std::max(1, up + down);

        painter.fillRect(QRect(x, top, 1, height), theme::waveform());
    }

    painter.restore();
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
    const int x = xForFrame(displayFrame());

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

int64_t TimelineWidget::displayFrame() const
{
    if (m_scrubFrame >= 0) {
        return m_scrubFrame;
    }
    return m_model != nullptr ? m_model->currentFrame() : 0;
}

void TimelineWidget::requestSeek(int64_t frame, bool force)
{
    if (!force) {
        if (frame == m_lastRequestedFrame) {
            return;
        }
        if (m_scrubThrottle.isValid() && m_scrubThrottle.elapsed() < kScrubThrottleMs) {
            return;
        }
    }

    m_lastRequestedFrame = frame;
    m_scrubThrottle.restart();
    emit scrubPreviewRequested(frame);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_scrubbing = true;
    emit scrubStarted();
    m_scrubThrottle.start();

    const int64_t frame = frameForX(event->position().toPoint().x());

    // Move the playhead now, before anything is decoded.
    m_scrubFrame = frame;
    update();

    // A click is a deliberate single position, so it is never throttled.
    requestSeek(frame, true);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_scrubbing) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const int64_t frame = frameForX(event->position().toPoint().x());

    // The playhead follows the pointer immediately and unconditionally. Only
    // the decode request below is throttled.
    if (frame != m_scrubFrame) {
        m_scrubFrame = frame;
        update();
    }

    requestSeek(frame, false);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_scrubbing) {
        m_scrubbing = false;

        // Land exactly where the pointer was let go, even if that position was
        // skipped by throttling.
        const int64_t frame = frameForX(event->position().toPoint().x());
        m_scrubFrame = frame;
        m_lastRequestedFrame = frame;
        emit scrubFinished(frame);

        // m_scrubFrame stays set until the model reports that frame, so the
        // playhead does not snap backwards to the last decoded position while
        // the final seek is still in flight.
        update();
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
