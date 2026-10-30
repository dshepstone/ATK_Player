#include "ui/TimelineWidget.h"

#include "media/WaveformData.h"

#include "timeline/TimelineModel.h"
#include "ui/Theme.h"

#include <QFontMetrics>
#include <QKeyEvent>
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
// lanes fit here without painting over waveform peaks. Measured below the
// waveform band, which compact mode drops.
constexpr int kTrackInsetTop = 34;
constexpr int kTrackInsetBottom = 18;
constexpr int kPlayheadHandleWidth = 9;
constexpr int kBookmarkMarkerWidth = 3;
constexpr int kRangeBandHeight = 6;
constexpr int kRangeLaneCount = 3;
constexpr int kRangeCapWidth = 3;
constexpr int kScrubLabelPadding = 6;

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

/// How far the pointer must travel after pressing inside the selection band
/// before it counts as a group drag rather than a click.
constexpr int kGroupDragThreshold = 4;
/// How far below the track the marker row (and so the selection band) reaches.
constexpr int kMarkerRowDepth = 12;

/// The translucent blue of the multi-marker selection band.
QColor selectionFill() { return QColor(64, 140, 255, 70); }
QColor selectionEdge() { return QColor(90, 160, 255, 200); }

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
    // Esc clears a marker selection before any window shortcut can take it.
    if (event->type() == QEvent::ShortcutOverride && !m_selectedMarkers.isEmpty()
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        event->accept();
        return true;
    }
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
        if (annotatedFrameAtPosition(help->pos()) >= 0) {
            QToolTip::showText(help->globalPos(),
                               m_markerDragEnabled
                                   ? tr("Notes or drawings on this frame.\nDrag to move them to another frame; "
                                        "hold Ctrl to copy.\nShift+drag across the timeline to select several "
                                        "markers and move them together.")
                                   : tr("Notes or drawings on this frame. Click to go there.\n"
                                        "Turn on Drag Markers to move them to another frame.\nShift+drag across "
                                        "the timeline to select several markers and move them together."),
                               this);
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
    return { 800, 84 + waveformRect().height() };
}

QSize TimelineWidget::minimumSizeHint() const
{
    return { 240, 84 + waveformRect().height() };
}

void TimelineWidget::setCompact(bool compact)
{
    if (m_compact == compact) return;
    m_compact = compact;
    updateGeometry();
    update();
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
                 std::max(0, rect().width() - 2 * kLabelMargin), m_compact ? 0 : kWaveformHeight);
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
    return rect().adjusted(kLabelMargin, kTrackInsetTop + waveformRect().height(), -kLabelMargin, -kTrackInsetBottom);
}

int64_t TimelineWidget::timeRulerStepSeconds(double pixelsPerSecond)
{
    constexpr double kMinSpacing = 80.0;
    for (const int64_t step : {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600})
        if (double(step) * pixelsPerSecond >= kMinSpacing) return step;
    int64_t step = 3600;
    while (double(step) * pixelsPerSecond < kMinSpacing && step < (int64_t(1) << 40)) step *= 2;
    return step;
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
    paintInOutMarks(painter);
    paintBookmarks(painter);
    paintHoldSpans(painter);
    paintAnnotatedFrames(painter);
    paintMarkerSelection(painter);
    paintFrameLabels(painter);
    paintPlayhead(painter);
    paintScrubFrameLabel(painter);
    paintMarkerDrag(painter);
    paintGroupDrag(painter);
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
        const int64_t startUs = mediaTimeForX(x) + m_waveformTimeOffsetUs;
        const int64_t endUs = mediaTimeForX(x + 1) + m_waveformTimeOffsetUs;

        if (endUs <= 0) {
            continue;
        }

        // Analysis fills in left to right, so the tail is simply not drawn yet
        // rather than being drawn as silence.
        if (startUs >= covered) {
            break;
        }

        const media::WaveformPeak peak =
            m_waveform->peakOverRange(level, std::max<int64_t>(0, startUs),
                                      std::max<int64_t>(1, endUs));
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

int TimelineWidget::inMarkX() const
{
    if (!m_model || !m_model->hasPlaybackMarks()) return -1;
    return xForFrame(m_model->playbackRange().startFrame);
}

int TimelineWidget::outMarkX() const
{
    if (!m_model || !m_model->hasPlaybackMarks()) return -1;
    return xForFrame(m_model->playbackRange().endFrame);
}

void TimelineWidget::paintInOutMarks(QPainter& painter)
{
    if (!m_model || !m_model->hasPlaybackMarks()) return;
    const QRect track = trackRect();
    const auto& viewport = m_model->viewport();
    const auto& range = m_model->playbackRange();
    if (range.endFrame < viewport.startFrame() || range.startFrame > viewport.endFrame()) return;

    // Outside the marks, the track is shaded; between them, a bar along the
    // bottom of the track shows what plays.
    const int left = range.startFrame >= viewport.startFrame() ? xForFrame(range.startFrame) : track.left();
    const int right = range.endFrame <= viewport.endFrame() ? xForFrame(range.endFrame) : track.right();
    const QRect inner = track.adjusted(1, 1, -1, -1);
    const QColor shade(0, 0, 0, 110);
    if (left > inner.left()) painter.fillRect(QRect(QPoint(inner.left(), inner.top()), QPoint(left - 1, inner.bottom())), shade);
    if (right < inner.right()) painter.fillRect(QRect(QPoint(right + 1, inner.top()), QPoint(inner.right(), inner.bottom())), shade);
    painter.fillRect(QRect(QPoint(left, track.bottom() - 2), QPoint(right, track.bottom())), theme::accent());

    // Bracket-shaped marks, [ at the in point and ] at the out point, reaching
    // a little above and below the track so they read at any zoom.
    painter.save();
    painter.setPen(QPen(theme::accent(), 2, Qt::SolidLine, Qt::FlatCap));
    const int top = track.top() - 4;
    const int bottom = track.bottom() + 1;
    constexpr int kFoot = 5;
    if (const int x = inMarkX(); x >= 0) {
        painter.drawLine(x, top, x, bottom);
        painter.drawLine(x, top + 1, x + kFoot, top + 1);
        painter.drawLine(x, bottom - 1, x + kFoot, bottom - 1);
    }
    if (const int x = outMarkX(); x >= 0) {
        painter.drawLine(x, top, x, bottom);
        painter.drawLine(x - kFoot, top + 1, x, top + 1);
        painter.drawLine(x - kFoot, bottom - 1, x, bottom - 1);
    }
    painter.restore();
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

void TimelineWidget::setAnnotatedFrames(QVector<int64_t> frames)
{
    if (frames == m_annotatedFrames) return;
    m_annotatedFrames = std::move(frames);
    // Markers that went away (undo, a clip change) drop out of the selection.
    setSelectedMarkerFrames(m_selectedMarkers);
    update();
}

QPair<int64_t, int64_t> TimelineWidget::markerSelectionRange() const
{
    if (m_selectedMarkers.isEmpty()) return {-1, -1};
    return {m_selectionFirst, m_selectionLast};
}

void TimelineWidget::setSelectedMarkerFrames(QVector<int64_t> frames)
{
    QVector<int64_t> kept;
    for (const int64_t frame : frames)
        if (m_annotatedFrames.contains(frame) && !kept.contains(frame)) kept.append(frame);
    std::sort(kept.begin(), kept.end());
    if (kept == m_selectedMarkers) return;
    m_selectedMarkers = std::move(kept);
    if (m_selectedMarkers.isEmpty()) {
        m_selectionFirst = m_selectionLast = -1;
    } else {
        // Keep a wider Shift+drag band, but always cover every marker.
        m_selectionFirst = m_selectionFirst < 0 ? m_selectedMarkers.first()
                                                : std::min(m_selectionFirst, m_selectedMarkers.first());
        m_selectionLast = m_selectionLast < 0 ? m_selectedMarkers.last()
                                              : std::max(m_selectionLast, m_selectedMarkers.last());
    }
    update();
    emit markerSelectionChanged(int(m_selectedMarkers.size()));
}

void TimelineWidget::revertGroupDrop()
{
    if (m_preDropSelection.isEmpty()) return;
    m_selectedMarkers = m_preDropSelection;
    m_selectionFirst = m_preDropFirst;
    m_selectionLast = m_preDropLast;
    m_preDropSelection.clear();
    update();
}

void TimelineWidget::clearMarkerSelection()
{
    m_selectionFirst = m_selectionLast = -1;
    if (m_selectedMarkers.isEmpty()) { update(); return; }
    m_selectedMarkers.clear();
    update();
    emit markerSelectionChanged(0);
}

int64_t TimelineWidget::clampGroupOffset(int64_t offset) const
{
    if (m_selectedMarkers.isEmpty()) return 0;
    const int64_t lowest = -m_selectedMarkers.first();
    const int64_t highest = std::max<int64_t>(0, lastFrame() - m_selectedMarkers.last());
    return std::clamp(offset, lowest, highest);
}

namespace {
// The pen colour of drawing markers and held-drawing bars.
QColor drawnFrameColor() { return QColor(255, 96, 64); }
} // namespace

void TimelineWidget::setHoldSpans(QVector<annotation::HoldSpan> spans)
{
    if (spans == m_holdSpans) return;
    m_holdSpans = std::move(spans);
    update();
}

void TimelineWidget::setMarkerDragEnabled(bool enabled)
{
    if (m_markerDragEnabled == enabled) return;
    m_markerDragEnabled = enabled;
    update();
}

void TimelineWidget::paintHoldSpans(QPainter& painter)
{
    if (m_model == nullptr || m_holdSpans.isEmpty()) return;
    // A thin bar just under the track, where the markers sit, from each
    // drawing to the frame before the next one.
    const QRect track = trackRect();
    QColor bar = drawnFrameColor();
    bar.setAlpha(110);
    // Clip each span to the frames in view, so a hold running off either end
    // of a zoomed timeline still shows its visible part.
    const int64_t visibleStart = m_model->viewport().startFrame();
    const int64_t visibleEnd = m_model->viewport().endFrame();
    for (const annotation::HoldSpan& span : m_holdSpans) {
        if (span.last <= span.first) continue;
        const int64_t first = std::max(span.first, visibleStart);
        const int64_t end = std::min({span.last + 1, lastFrame(), visibleEnd});
        if (end <= first) continue;
        const int left = xForFrame(first);
        const int right = xForFrame(end);
        if (left < 0 || right <= left) continue;
        painter.fillRect(QRect(left, track.bottom() + 1, right - left, 2), bar);
    }
}

void TimelineWidget::paintAnnotatedFrames(QPainter& painter)
{
    if (m_model == nullptr || m_annotatedFrames.isEmpty()) return;
    // A small pen-coloured wedge under the track: distinct from bookmark
    // markers above it, so "has a drawing" and "has a bookmark" never blur.
    // With Drag Markers on the wedges grow and gain an outline: handles.
    const QRect track = trackRect();
    const qreal half = m_markerDragEnabled ? 4.5 : 3.5;
    const qreal height = m_markerDragEnabled ? 7.0 : 5.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(m_markerDragEnabled ? QPen(QColor(255, 255, 255, 200), 1.0) : QPen(Qt::NoPen));
    painter.setBrush(drawnFrameColor());
    for (const int64_t frame : m_annotatedFrames) {
        const int x = xForFrame(frame);
        if (x < 0) continue;
        const QPointF tip(x + 0.5, track.bottom() + 1.0);
        painter.drawPolygon(QPolygonF{tip, tip + QPointF(-half, height), tip + QPointF(half, height)});
    }
    painter.restore();
}

int64_t TimelineWidget::annotatedFrameAtPosition(const QPoint& point) const
{
    if (m_model == nullptr || m_annotatedFrames.isEmpty()) return -1;
    // The wedge is 7 px wide and 5 px tall; the reach is a little more so it
    // is easy to pick up, and the nearest marker wins where several crowd.
    constexpr int kReachX = 5;
    const QRect track = trackRect();
    if (point.y() < track.bottom() || point.y() > track.bottom() + 12) return -1;
    int64_t best = -1;
    int bestDistance = kReachX + 1;
    for (const int64_t frame : m_annotatedFrames) {
        const int x = xForFrame(frame);
        if (x < 0) continue;
        const int distance = std::abs(point.x() - x);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = frame;
        }
    }
    return best;
}

namespace {
/// The band over frames [first, last] across the track and the marker row,
/// a frame cell wide at each end so a single frame still reads as a block.
QRect bandRect(const QRect& track, int left, int right, double pixelsPerFrame)
{
    const int pad = std::max(3, static_cast<int>(std::lround(pixelsPerFrame / 2.0)));
    return QRect(QPoint(left - pad, track.top()), QPoint(right + pad, track.bottom() + kMarkerRowDepth));
}
} // namespace

QRect TimelineWidget::markerSelectionRect() const
{
    if (m_model == nullptr || m_selectedMarkers.isEmpty()) return {};
    const auto& viewport = m_model->viewport();
    const int64_t first = std::max(m_selectionFirst, viewport.startFrame());
    const int64_t last = std::min(m_selectionLast, viewport.endFrame());
    if (last < first) return {};
    const QRect track = trackRect();
    const double pixelsPerFrame = viewport.visibleFrameCount() > 1
        ? double(std::max(0, track.width() - 1)) / double(viewport.visibleFrameCount() - 1) : 0.0;
    return bandRect(track, xForFrame(first), xForFrame(last), pixelsPerFrame);
}

void TimelineWidget::paintMarkerSelection(QPainter& painter)
{
    if (m_model == nullptr) return;
    const QRect track = trackRect();
    const auto& viewport = m_model->viewport();
    const double pixelsPerFrame = viewport.visibleFrameCount() > 1
        ? double(std::max(0, track.width() - 1)) / double(viewport.visibleFrameCount() - 1) : 0.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);

    QRect band;
    if (m_rubberBanding && m_rubberFrom >= 0 && m_rubberTo >= 0) {
        const int64_t first = std::max(std::min(m_rubberFrom, m_rubberTo), viewport.startFrame());
        const int64_t last = std::min(std::max(m_rubberFrom, m_rubberTo), viewport.endFrame());
        if (last >= first) band = bandRect(track, xForFrame(first), xForFrame(last), pixelsPerFrame);
    } else {
        band = markerSelectionRect();
    }
    if (!band.isEmpty()) {
        painter.fillRect(band, selectionFill());
        painter.setPen(selectionEdge());
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(band.adjusted(0, 0, -1, -1));
    }

    // Selected wedges again, outlined in blue, so they stand out from the
    // unselected ones that happen to sit inside the band.
    if (!m_selectedMarkers.isEmpty()) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(selectionEdge().lighter(130), 1.5));
        painter.setBrush(drawnFrameColor().lighter(115));
        const qreal half = m_markerDragEnabled ? 4.5 : 3.5;
        const qreal height = m_markerDragEnabled ? 7.0 : 5.0;
        for (const int64_t frame : m_selectedMarkers) {
            const int x = xForFrame(frame);
            if (x < 0) continue;
            const QPointF tip(x + 0.5, track.bottom() + 1.0);
            painter.drawPolygon(QPolygonF{tip, tip + QPointF(-half, height), tip + QPointF(half, height)});
        }
    }
    painter.restore();
}

void TimelineWidget::paintGroupDrag(QPainter& painter)
{
    if (!m_groupDragActive || m_groupDragOffset == 0 || m_model == nullptr) return;
    const QRect track = trackRect();
    const auto& viewport = m_model->viewport();
    const double pixelsPerFrame = viewport.visibleFrameCount() > 1
        ? double(std::max(0, track.width() - 1)) / double(viewport.visibleFrameCount() - 1) : 0.0;
    painter.save();
    // Where the band lands.
    const int64_t first = std::max(m_selectionFirst + m_groupDragOffset, viewport.startFrame());
    const int64_t last = std::min(m_selectionLast + m_groupDragOffset, viewport.endFrame());
    if (last >= first) {
        const QRect band = bandRect(track, xForFrame(first), xForFrame(last), pixelsPerFrame);
        QColor fill = selectionFill();
        fill.setAlpha(110);
        painter.fillRect(band, fill);
        painter.setPen(QPen(theme::playhead(), 1, Qt::DashLine));
        painter.drawRect(band.adjusted(0, 0, -1, -1));
    }
    // Ghost wedges where each marker lands.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme::playhead());
    for (const int64_t frame : m_selectedMarkers) {
        const int x = xForFrame(frame + m_groupDragOffset);
        if (x < 0) continue;
        const QPointF tip(x + 0.5, track.bottom() + 1.0);
        painter.drawPolygon(QPolygonF{tip, tip + QPointF(-4.5, 6.5), tip + QPointF(4.5, 6.5)});
    }

    QFont bold = font();
    bold.setBold(true);
    painter.setFont(bold);
    const QString sign = m_groupDragOffset > 0 ? QStringLiteral("+") : QString();
    const qsizetype count = m_selectedMarkers.size();
    const QString what = count == 1 ? tr("1 marker") : tr("%1 markers").arg(count);
    const QString text = (m_groupDragCopy ? tr("Copy %1 %2") : tr("Move %1 %2"))
                             .arg(what, sign + QString::number(m_groupDragOffset));
    const QFontMetrics metrics(bold);
    const int width = metrics.horizontalAdvance(text) + 2 * kScrubLabelPadding;
    const int height = metrics.height() + 4;
    const int anchor = xForFrame(std::clamp(m_selectionFirst + m_groupDragOffset, viewport.startFrame(),
                                            viewport.endFrame()));
    const int left = std::clamp(anchor - width / 2, 0, std::max(0, rect().width() - width));
    const QRect badge(left, track.top() - 4 - height, width, height);
    painter.drawRoundedRect(badge, 3, 3);
    painter.setPen(theme::timelineTrack());
    painter.drawText(badge, Qt::AlignCenter, text);
    painter.restore();
}

void TimelineWidget::paintMarkerDrag(QPainter& painter)
{
    if (m_markerDragFrom < 0 || m_markerDragTo < 0 || m_markerDragTo == m_markerDragFrom) return;
    const int x = xForFrame(m_markerDragTo);
    if (x < 0) return;
    const QRect track = trackRect();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A dashed guide through the track and a solid wedge where the drop lands.
    QPen guide(theme::playhead(), 1, Qt::DashLine);
    painter.setPen(guide);
    painter.drawLine(QPointF(x + 0.5, track.top()), QPointF(x + 0.5, track.bottom() + 1.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme::playhead());
    const QPointF tip(x + 0.5, track.bottom() + 1.0);
    painter.drawPolygon(QPolygonF{tip, tip + QPointF(-4.5, 6.5), tip + QPointF(4.5, 6.5)});

    QFont bold = font();
    bold.setBold(true);
    painter.setFont(bold);
    const QString text = (m_markerDragCopy ? tr("Copy to %1") : tr("Move to %1")).arg(m_markerDragTo + 1);
    const QFontMetrics metrics(bold);
    const int width = metrics.horizontalAdvance(text) + 2 * kScrubLabelPadding;
    const int height = metrics.height() + 4;
    const int left = std::clamp(x - width / 2, 0, std::max(0, rect().width() - width));
    const QRect badge(left, track.top() - 4 - height, width, height);
    painter.drawRoundedRect(badge, 3, 3);
    painter.setPen(theme::timelineTrack());
    painter.drawText(badge, Qt::AlignCenter, text);
    painter.restore();
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

QString TimelineWidget::scrubFrameLabelText() const
{
    // One-based, matching the FRAME field and the ruler labels.
    return QString::number(displayFrame() + 1);
}

QRect TimelineWidget::scrubFrameLabelRect() const
{
    if (!m_model || m_model->frameCount() <= 0) return {};
    const int x = xForFrame(displayFrame());
    if (x < 0) return {};
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics metrics(bold);
    const int width = metrics.horizontalAdvance(scrubFrameLabelText()) + 2 * kScrubLabelPadding;
    const int height = metrics.height() + 4;
    // Centred on the playhead, sitting just above the track so the line runs
    // up into it, and kept inside the widget at either end of the ruler.
    const int left = std::clamp(x - width / 2, 0, std::max(0, rect().width() - width));
    return QRect(left, trackRect().top() - 4 - height, width, height);
}

void TimelineWidget::paintScrubFrameLabel(QPainter& painter)
{
    const QRect badge = scrubFrameLabelRect();
    if (badge.isEmpty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Filled while scrubbing; at rest, an outlined tag in the playhead's
    // colour, so the frame number is always there without shouting.
    if (m_scrubbing) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::playhead());
    } else {
        painter.setPen(QPen(theme::playhead(), 1));
        painter.setBrush(theme::timelineTrack());
    }
    painter.drawRoundedRect(QRectF(badge).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    QFont bold = font();
    bold.setBold(true);
    painter.setFont(bold);
    painter.setPen(m_scrubbing ? theme::timelineTrack() : theme::playhead());
    painter.drawText(badge, Qt::AlignCenter, scrubFrameLabelText());
    painter.restore();
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
        const auto rate = m_model ? m_model->frameRate() : media::FrameRate{};
        if (rate.isValid()) {
            // Round times at their true places: the smallest step of a
            // stopwatch-like series that keeps labels ~80 px apart, so every
            // label differs and none sits between two seconds.
            const double pixelsPerSecond = pixelsPerFrame * double(rate.numerator) / double(rate.denominator);
            const int64_t stepSeconds = timeRulerStepSeconds(pixelsPerSecond);
            const auto& viewport = m_model->viewport();
            const auto secondsAt = [&](int64_t frame) {
                return double(frame) * double(rate.denominator) / double(rate.numerator);
            };
            const int64_t first = static_cast<int64_t>(std::ceil(secondsAt(viewport.startFrame()) / double(stepSeconds)))
                                * stepSeconds;
            for (int64_t s = first; s <= secondsAt(viewport.endFrame()) + 1e-9; s += stepSeconds) {
                const auto frame = static_cast<int64_t>(
                    std::llround(double(s) * double(rate.numerator) / double(rate.denominator)));
                const int x = xForFrame(std::clamp(frame, viewport.startFrame(), viewport.endFrame()));
                if (x < 0) continue;
                painter.drawText(QRect(x - 36, waveformRect().top(), 72, 14), Qt::AlignHCenter | Qt::AlignTop,
                                 rulerTime(s * 1'000'000));
            }
        } else {
            const int divisions = std::max(1, track.width() / 96);
            for (int i = 0; i <= divisions; ++i) {
                const int x = track.left() + ((track.width() - 1) * i) / divisions;
                painter.drawText(QRect(x - 36, waveformRect().top(), 72, 14),
                                 Qt::AlignHCenter | Qt::AlignTop, rulerTime(mediaTimeForX(x)));
            }
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
    const QPoint clickPos = event->position().toPoint();
    if (m_model && m_model->frameCount() > 0) {
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            // Shift+click on a marker adds it to the selection or takes it out;
            // Shift+drag anywhere else sweeps out a range of markers.
            const int64_t marker = annotatedFrameAtPosition(clickPos);
            if (marker >= 0) {
                QVector<int64_t> frames = m_selectedMarkers;
                if (frames.contains(marker)) {
                    frames.removeAll(marker);
                    // Shrink the band back to the markers that are left.
                    m_selectionFirst = m_selectionLast = -1;
                } else {
                    frames.append(marker);
                }
                setSelectedMarkerFrames(frames);
                return;
            }
            m_rubberBanding = true;
            m_rubberFrom = m_rubberTo = std::clamp<int64_t>(frameForX(clickX), 0, lastFrame());
            update();
            return;
        }
        if (!m_selectedMarkers.isEmpty()) {
            if (markerSelectionRect().contains(clickPos)) {
                m_groupPressed = true;
                m_groupDragActive = false;
                m_groupPressX = clickX;
                m_groupPressFrame = frameForX(clickX);
                m_groupDragOffset = 0;
                m_groupDragCopy = event->modifiers().testFlag(Qt::ControlModifier);
                return;
            }
            // A click anywhere else drops the selection and does what it
            // always did.
            clearMarkerSelection();
        }
    }
    if (m_model) {
        const int64_t marker = annotatedFrameAtPosition(clickPos);
        if (marker >= 0 && !m_markerDragEnabled) {
            // Markers are fixed: a click goes to that frame.
            m_scrubFrame = marker;
            m_lastRequestedFrame = marker;
            emit scrubStarted();
            emit scrubFinished(marker);
            return;
        }
        if (marker >= 0) {
            m_markerDragFrom = marker;
            m_markerDragTo = marker;
            m_markerDragCopy = event->modifiers().testFlag(Qt::ControlModifier);
            setCursor(Qt::ClosedHandCursor);
            return;
        }
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
    if (m_rubberBanding) {
        const int64_t frame = std::clamp<int64_t>(frameForX(event->position().toPoint().x()), 0, lastFrame());
        if (frame != m_rubberTo) {
            m_rubberTo = frame;
            update();
        }
        return;
    }
    if (m_groupPressed) {
        const int x = event->position().toPoint().x();
        if (!m_groupDragActive && std::abs(x - m_groupPressX) >= kGroupDragThreshold) m_groupDragActive = true;
        if (m_groupDragActive) {
            const int64_t offset = clampGroupOffset(frameForX(x) - m_groupPressFrame);
            const bool copy = event->modifiers().testFlag(Qt::ControlModifier);
            if (offset != m_groupDragOffset || copy != m_groupDragCopy) {
                m_groupDragOffset = offset;
                m_groupDragCopy = copy;
                update();
            }
            setCursor(copy ? Qt::DragCopyCursor : Qt::ClosedHandCursor);
        }
        return;
    }
    if (m_markerDragFrom >= 0) {
        const int64_t frame = std::clamp<int64_t>(frameForX(event->position().toPoint().x()), 0, lastFrame());
        const bool copy = event->modifiers().testFlag(Qt::ControlModifier);
        if (frame != m_markerDragTo || copy != m_markerDragCopy) {
            m_markerDragTo = frame;
            m_markerDragCopy = copy;
            setCursor(copy ? Qt::DragCopyCursor : Qt::ClosedHandCursor);
            update();
        }
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
    if (event->button() == Qt::LeftButton && m_rubberBanding) {
        m_rubberBanding = false;
        const int64_t first = std::min(m_rubberFrom, m_rubberTo);
        const int64_t last = std::max(m_rubberFrom, m_rubberTo);
        m_rubberFrom = m_rubberTo = -1;
        QVector<int64_t> frames;
        for (const int64_t frame : m_annotatedFrames)
            if (frame >= first && frame <= last) frames.append(frame);
        clearMarkerSelection();
        if (!frames.isEmpty()) {
            m_selectionFirst = first;
            m_selectionLast = last;
            setSelectedMarkerFrames(frames);
        }
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && m_groupPressed) {
        const bool dragged = m_groupDragActive;
        const int64_t offset = m_groupDragOffset;
        const bool copy = event->modifiers().testFlag(Qt::ControlModifier);
        m_groupPressed = false;
        m_groupDragActive = false;
        m_groupDragOffset = 0;
        m_groupDragCopy = false;
        unsetCursor();
        if (dragged && offset != 0) {
            QVector<qint64> frames;
            QVector<int64_t> landed;
            for (const int64_t frame : m_selectedMarkers) {
                frames.append(frame);
                landed.append(frame + offset);
            }
            // The selection follows the markers to where they land, so the
            // group can be nudged again straight away. The original is kept
            // until the owner has applied the move, in case it is refused.
            m_preDropSelection = m_selectedMarkers;
            m_preDropFirst = m_selectionFirst;
            m_preDropLast = m_selectionLast;
            m_selectedMarkers = landed;
            m_selectionFirst += offset;
            m_selectionLast += offset;
            update();
            emit annotatedFramesDropped(frames, offset, copy);
            m_preDropSelection.clear();
            setSelectedMarkerFrames(m_selectedMarkers);
            return;
        }
        if (!dragged) {
            // A click inside the band: drop the selection and go there.
            const int64_t frame = snapFrame(frameForX(event->position().toPoint().x()),
                                            event->position().toPoint().x());
            clearMarkerSelection();
            m_scrubFrame = frame;
            m_lastRequestedFrame = frame;
            emit scrubStarted();
            emit scrubFinished(frame);
        }
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && m_markerDragFrom >= 0) {
        const int64_t from = m_markerDragFrom;
        const int64_t to = m_markerDragTo;
        const bool copy = event->modifiers().testFlag(Qt::ControlModifier);
        m_markerDragFrom = -1;
        m_markerDragTo = -1;
        m_markerDragCopy = false;
        unsetCursor();
        update();
        if (to >= 0 && to != from) {
            emit annotatedFrameDropped(from, to, copy);
        } else {
            // A click on a marker without dragging goes to that frame.
            m_scrubFrame = from;
            m_lastRequestedFrame = from;
            emit scrubStarted();
            emit scrubFinished(from);
        }
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

void TimelineWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && (!m_selectedMarkers.isEmpty() || m_rubberBanding)) {
        m_rubberBanding = false;
        m_groupPressed = false;
        m_groupDragActive = false;
        unsetCursor();
        clearMarkerSelection();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
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
