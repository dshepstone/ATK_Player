#include "ui/TimelineWidget.h"

#include "media/WaveformData.h"

#include "timeline/TimelineModel.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QToolTip>

#include <algorithm>
#include <cmath>
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
// A compact bookmark strip sits between the waveform and ruler. Three range
// lanes fit here without painting over waveform peaks.
constexpr int kTrackInsetTop = 34 + kWaveformHeight;
constexpr int kTrackInsetBottom = 18;
constexpr int kPlayheadHandleWidth = 9;
constexpr int kBookmarkMarkerWidth = 3;
constexpr int kRangeBandHeight = 6;
constexpr int kRangeLaneCount = 3;
constexpr int kRangeCapWidth = 3;

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

QString rulerTime(int64_t us)
{
    const int64_t totalSeconds = std::max<int64_t>(0, us) / 1'000'000;
    const int64_t hours = totalSeconds / 3600;
    const int64_t minutes = (totalSeconds / 60) % 60;
    const int64_t seconds = totalSeconds % 60;
    return hours > 0
        ? QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
    setFocusPolicy(Qt::ClickFocus);
}

TimelineWidget::~TimelineWidget() = default;

bool TimelineWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip && m_model) {
        auto* help = static_cast<QHelpEvent*>(event);
        const quint64 id = bookmarkAtPosition(help->pos());
        if (const timeline::Bookmark* bookmark = m_model->bookmark(id)) {
            QString text = QStringLiteral("%1\n%2 %3")
                .arg(bookmark->displayLabel(), bookmark->isRange() ? tr("Frames") : tr("Frame"),
                     bookmark->frameLabel());
            if (!bookmark->note.isEmpty()) text += QLatin1Char('\n') + bookmark->note;
            QToolTip::showText(help->globalPos(), text, this);
            return true;
        }
        QToolTip::hideText();
    }
    return QWidget::event(event);
}

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
        connect(m_model, &timeline::TimelineModel::viewportChanged, this, repaint);
    }
}

void TimelineWidget::zoomIn()
{
    if (m_model) m_model->zoomViewport(1.5, m_model->currentFrame());
}

void TimelineWidget::zoomOut()
{
    if (m_model) m_model->zoomViewport(1.0 / 1.5, m_model->currentFrame());
}

void TimelineWidget::fitEntire()
{
    if (m_model) m_model->fitViewport();
}

QSize TimelineWidget::sizeHint() const
{
    return { 800, 84 + kWaveformHeight };
}

QSize TimelineWidget::minimumSizeHint() const
{
    return { 240, 84 + kWaveformHeight };
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
    const int spanPx = std::max(0, track.width() - 1);
    if (spanPx <= 0 || m_mediaDurationUs <= 0) {
        return 0;
    }
    const double fraction = std::clamp(double(x - track.left()) / double(spanPx), 0.0, 1.0);
    const int64_t frame = m_model ? m_model->viewport().frameAtFraction(fraction) : 0;
    const auto rate = m_model ? m_model->frameRate() : media::FrameRate{};
    if (rate.isValid()) {
        const long double us = static_cast<long double>(frame) * 1'000'000.0L
                             * rate.denominator / rate.numerator;
        return std::clamp<int64_t>(static_cast<int64_t>(us), 0, m_mediaDurationUs);
    }
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
    const int spanPx = std::max(0, track.width() - 1);
    if (!m_model || spanPx <= 0) {
        return track.left();
    }
    if (!m_model->viewport().contains(frame)) return -1;
    const double t = m_model->viewport().fractionForFrame(frame);
    return track.left() + static_cast<int>(std::llround(t * spanPx));
}

int64_t TimelineWidget::frameForX(int x) const
{
    const QRect track = trackRect();
    const int spanPx = std::max(0, track.width() - 1);
    if (!m_model || spanPx <= 0) {
        return 0;
    }
    const double t = static_cast<double>(std::clamp(x, track.left(), track.right()) - track.left())
                   / static_cast<double>(spanPx);
    return m_model->viewport().frameAtFraction(t);
}

void TimelineWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(event->rect(), theme::panelBackground());

    paintWaveform(painter);
    paintTrack(painter);
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
    const int64_t visibleUs = std::max<int64_t>(1, mediaTimeForX(band.right()) - mediaTimeForX(band.left()));
    const int64_t usPerPixel = std::max<int64_t>(1, visibleUs / band.width());
    const int level = m_waveform->levelForBucketDuration(usPerPixel);

    const int64_t covered = m_waveform->coveredUs();

    // Normalised against the file's own peak so dialogue mastered below full
    // scale is still readable -- see WaveformData::displayGain().
    const float gain = m_waveform->displayGain();

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

        const int up = std::clamp(int(peak.maximum * gain * halfHeight), 0, halfHeight);
        const int down = std::clamp(int(-peak.minimum * gain * halfHeight), 0, halfHeight);
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

    if (!m_model || m_model->viewport().visibleFrameCount() <= 1) return;
    const auto& viewport = m_model->viewport();
    const double pixelsPerFrame = double(std::max(0, track.width() - 1))
                                / double(viewport.visibleFrameCount() - 1);

    // A subtle frame cell makes the displayed frame unambiguous at animation
    // review zoom levels without turning the ruler into a checkerboard.
    if (pixelsPerFrame >= 6.0 && viewport.contains(displayFrame())) {
        const int x = xForFrame(displayFrame());
        const int cellWidth = std::max(1, static_cast<int>(std::llround(pixelsPerFrame)));
        painter.fillRect(QRect(x, track.top() + 1, cellWidth, track.height() - 2),
                         theme::timelineRange());
    }

    int64_t tickStep = 1;
    int64_t labelStep = 1;
    if (pixelsPerFrame >= 18.0) {
        tickStep = labelStep = 1;
    } else if (pixelsPerFrame >= 6.0) {
        tickStep = 1;
        labelStep = pixelsPerFrame >= 12.0 ? 5 : 10;
    } else {
        const double desiredFrames = 64.0 / std::max(0.001, pixelsPerFrame);
        const double magnitude = std::pow(10.0, std::floor(std::log10(desiredFrames)));
        const double normalised = desiredFrames / magnitude;
        const int nice = normalised <= 1.0 ? 1 : normalised <= 2.0 ? 2 : normalised <= 5.0 ? 5 : 10;
        labelStep = std::max<int64_t>(1, static_cast<int64_t>(nice * magnitude));
        tickStep = std::max<int64_t>(1, labelStep / 5);
    }

    painter.setPen(theme::tickMark());
    const int64_t firstTick = ((viewport.startFrame() + tickStep - 1) / tickStep) * tickStep;
    for (int64_t frame = firstTick; frame <= viewport.endFrame(); frame += tickStep) {
        const int x = xForFrame(frame);
        const bool major = frame % labelStep == 0;
        painter.drawLine(x, track.bottom() - (major ? 8 : 4), x, track.bottom() - 1);
        if (major) {
            painter.drawText(QRect(x - 28, track.top() + 1, 56, 14),
                             Qt::AlignHCenter | Qt::AlignTop, QString::number(frame + 1));
        }
    }
}

void TimelineWidget::paintBookmarks(QPainter& painter)
{
    if (m_model == nullptr || lastFrame() <= 0) {
        return;
    }

    const QRect track = trackRect();
    for (const timeline::Bookmark& bookmark : m_model->bookmarks()) {
        if (bookmark.isRange()) {
            const QRect band = rangeBookmarkRect(bookmark.id);
            if (!band.isEmpty()) {
                QColor color = bookmark.hasColor() ? timeline::bookmarkColor(bookmark.colorIndex)
                                                    : theme::accent();
                QColor fill = color;
                fill.setAlpha(bookmark.id == m_selectedBookmarkId ? 150 : 85);
                painter.fillRect(band, fill);
                painter.fillRect(rangeBookmarkStartCapRect(bookmark.id), color);
                painter.fillRect(rangeBookmarkEndCapRect(bookmark.id), color);
                if (bookmark.id == m_selectedBookmarkId) {
                    painter.setPen(color.lighter(130));
                    painter.drawRect(band.adjusted(0, 0, -1, -1));
                }
                if (rangeBookmarkShowsLabel(bookmark.id)) {
                    painter.setPen(palette().text().color());
                    painter.drawText(band.adjusted(kRangeCapWidth + 3, -5, -kRangeCapWidth - 2, 5),
                                     Qt::AlignLeft | Qt::AlignVCenter, bookmark.displayLabel());
                }
            }
            continue;
        }
        const int x = xForFrame(bookmark.frame);
        if (x < 0) continue;
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
    if (x < 0) return;

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
                     QString::number(m_model ? m_model->viewport().startFrame() + 1 : 1));

    // Last frame, right of the track.
    painter.drawText(QRect(track.right() + 6, track.top(), kLabelMargin - 6, track.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString::number((m_model ? m_model->viewport().endFrame() : last) + 1));

    // Time labels describe the viewport rather than the whole source. Their
    // pixel spacing is bounded, so zooming never turns the ruler into a wall
    // of text while still revealing finer time positions as the span narrows.
    const double pixelsPerFrame = m_model && m_model->viewport().visibleFrameCount() > 1
        ? double(track.width() - 1) / double(m_model->viewport().visibleFrameCount() - 1)
        : 0.0;
    if (m_mediaDurationUs > 0 && track.width() > 0 && pixelsPerFrame < 6.0) {
        const int divisions = std::max(1, track.width() / 96);
        for (int i = 0; i <= divisions; ++i) {
            const int x = track.left() + ((track.width() - 1) * i) / divisions;
            const QString label = rulerTime(mediaTimeForX(x));
            painter.drawText(QRect(x - 36, waveformRect().top(), 72, 14),
                             Qt::AlignHCenter | Qt::AlignTop, label);
        }
    }
}

QRect TimelineWidget::rangeBookmarkRect(quint64 id) const
{
    if (!m_model) return {};
    const timeline::Bookmark* bookmark = m_model->bookmark(id);
    if (!bookmark || !bookmark->isRange()) return {};
    const auto& viewport = m_model->viewport();
    if (bookmark->endFrame < viewport.startFrame() || bookmark->frame > viewport.endFrame()) return {};
    const QRect track = trackRect();
    const qreal count = std::max<qreal>(1.0, viewport.visibleFrameCount());
    const auto boundaryX = [&](qreal boundary) {
        return track.left() + (boundary - viewport.startFrame()) * track.width() / count;
    };
    const int left = qRound(boundaryX(std::max<int64_t>(bookmark->frame, viewport.startFrame())));
    const int right = qRound(boundaryX(std::min<int64_t>(bookmark->endFrame + 1,
                                                         viewport.endFrame() + 1)));
    int rangeOrdinal = 0;
    for (const timeline::Bookmark& candidate : m_model->bookmarks()) {
        if (!candidate.isRange()) continue;
        if (candidate.id == id) break;
        ++rangeOrdinal;
    }
    const int lane = rangeOrdinal % kRangeLaneCount;
    return QRect(left, track.top() - 7 - lane * (kRangeBandHeight + 1),
                 std::max(1, right - left), kRangeBandHeight);
}

QRect TimelineWidget::rangeBookmarkStartCapRect(quint64 id) const
{
    const QRect band = rangeBookmarkRect(id);
    return band.isEmpty() ? QRect{} : QRect(band.left(), band.top() - 1, kRangeCapWidth, band.height() + 2);
}

QRect TimelineWidget::rangeBookmarkEndCapRect(quint64 id) const
{
    const QRect band = rangeBookmarkRect(id);
    return band.isEmpty() ? QRect{} : QRect(band.right() - kRangeCapWidth + 1, band.top() - 1,
                                            kRangeCapWidth, band.height() + 2);
}

bool TimelineWidget::rangeBookmarkShowsLabel(quint64 id) const
{
    if (!m_model) return false;
    const timeline::Bookmark* bookmark = m_model->bookmark(id);
    const QRect band = rangeBookmarkRect(id);
    if (!bookmark || band.isEmpty()) return false;
    return band.width() >= fontMetrics().horizontalAdvance(bookmark->displayLabel())
                          + 2 * kRangeCapWidth + 10;
}

quint64 TimelineWidget::bookmarkAtPosition(const QPoint& point) const
{
    if (!m_model) return 0;
    for (const timeline::Bookmark& bookmark : m_model->bookmarks()) {
        if (bookmark.isRange() && rangeBookmarkRect(bookmark.id).adjusted(-2, -2, 2, 2).contains(point))
            return bookmark.id;
    }
    for (const timeline::Bookmark& bookmark : m_model->bookmarks()) {
        if (!bookmark.isRange() && std::abs(xForFrame(bookmark.frame) - point.x()) <= 5)
            return bookmark.id;
    }
    return 0;
}

int64_t TimelineWidget::displayFrame() const
{
    if (m_scrubFrame >= 0) {
        return m_scrubFrame;
    }
    return m_model != nullptr ? m_model->currentFrame() : 0;
}

int64_t TimelineWidget::snapFrame(int64_t frame, int x) const
{
    if (!m_bookmarkSnapEnabled || !m_model) return frame;
    constexpr int kSnapPixels = 8;
    int64_t best = frame;
    int bestDistance = kSnapPixels + 1;
    for (const timeline::Bookmark& bookmark : m_model->bookmarks()) {
        for (const int64_t candidate : {bookmark.frame, bookmark.endFrame}) {
            if (!bookmark.isRange() && candidate != bookmark.frame) continue;
            const int markerX = xForFrame(candidate);
            if (markerX < 0) continue;
            const int distance = std::abs(markerX - x);
            if (distance <= kSnapPixels && distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        }
    }
    return best;
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
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanX = event->position().toPoint().x();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int clickX = event->position().toPoint().x();
    if (m_model) {
        const quint64 id = bookmarkAtPosition(event->position().toPoint());
        if (id != 0) {
            setSelectedBookmark(id);
            emit bookmarkSelected(id);
            return;
        }
    }
    m_scrubbing = true;
    emit scrubStarted();
    m_scrubThrottle.start();

    const int64_t frame = snapFrame(frameForX(clickX), clickX);

    // Move the playhead now, before anything is decoded.
    m_scrubFrame = frame;
    update();

    // A click is a deliberate single position, so it is never throttled.
    requestSeek(frame, true);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning && m_model) {
        const int x = event->position().toPoint().x();
        const int width = std::max(1, trackRect().width());
        const int64_t delta = static_cast<int64_t>(std::llround(
            double(m_lastPanX - x) * m_model->viewport().visibleFrameCount() / width));
        if (delta != 0) { m_model->panViewport(delta); m_lastPanX = x; }
        return;
    }
    if (!m_scrubbing) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const int x = event->position().toPoint().x();
    const int64_t frame = snapFrame(frameForX(x), x);

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
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        return;
    }
    if (event->button() == Qt::LeftButton && m_scrubbing) {
        m_scrubbing = false;

        // Land exactly where the pointer was let go, even if that position was
        // skipped by throttling.
        const int x = event->position().toPoint().x();
        const int64_t frame = snapFrame(frameForX(x), x);
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

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_model || event->angleDelta().y() == 0) { QWidget::wheelEvent(event); return; }
    const int steps = event->angleDelta().y() / 120;
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const int64_t anchor = frameForX(event->position().toPoint().x());
        m_model->zoomViewport(std::pow(1.25, steps), anchor);
    } else if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int64_t amount = std::max<int64_t>(1, m_model->viewport().visibleFrameCount() / 10);
        m_model->panViewport(-steps * amount);
    } else { QWidget::wheelEvent(event); return; }
    event->accept();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_model == nullptr || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    const quint64 id = bookmarkAtPosition(event->position().toPoint());
    if (id != 0) {
        emit bookmarkActivated(id);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace atk::ui
