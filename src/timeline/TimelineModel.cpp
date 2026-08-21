#include "timeline/TimelineModel.h"

#include "core/Logging.h"

#include <algorithm>

namespace atk::timeline {

TimelineModel::TimelineModel(QObject* parent)
    : QObject(parent)
{
}

void TimelineModel::setFrameCount(int64_t count)
{
    // Any caller other than setPlaceholderExtent() is supplying a real extent,
    // so the placeholder marking must not survive it.
    setPlaceholder(false);

    const int64_t clamped = std::max<int64_t>(count, 0);
    if (m_frameCount == clamped) {
        return;
    }
    m_frameCount = clamped;
    m_viewport.reset(m_frameCount);
    emit frameCountChanged(m_frameCount);
    emit viewportChanged(m_viewport.startFrame(), m_viewport.endFrame());

    // The playhead and range may now sit outside the source.
    if (m_range.enabled) {
        PlaybackRange adjusted = m_range;
        adjusted.startFrame = std::min(adjusted.startFrame, std::max<int64_t>(lastFrame(), 0));
        adjusted.endFrame   = std::min(adjusted.endFrame,   std::max<int64_t>(lastFrame(), 0));
        setPlaybackRange(adjusted);
    }
    setCurrentFrame(m_currentFrame);
}

void TimelineModel::fitViewport()
{
    const auto oldStart = m_viewport.startFrame();
    const auto oldEnd = m_viewport.endFrame();
    m_viewport.fit();
    if (oldStart != m_viewport.startFrame() || oldEnd != m_viewport.endFrame())
        emit viewportChanged(m_viewport.startFrame(), m_viewport.endFrame());
}

void TimelineModel::zoomViewport(double factor, int64_t anchorFrame)
{
    const auto oldStart = m_viewport.startFrame();
    const auto oldEnd = m_viewport.endFrame();
    m_viewport.zoom(factor, anchorFrame);
    if (oldStart != m_viewport.startFrame() || oldEnd != m_viewport.endFrame())
        emit viewportChanged(m_viewport.startFrame(), m_viewport.endFrame());
}

void TimelineModel::panViewport(int64_t deltaFrames)
{
    const auto oldStart = m_viewport.startFrame();
    const auto oldEnd = m_viewport.endFrame();
    m_viewport.pan(deltaFrames);
    if (oldStart != m_viewport.startFrame() || oldEnd != m_viewport.endFrame())
        emit viewportChanged(m_viewport.startFrame(), m_viewport.endFrame());
}

void TimelineModel::setViewportRange(int64_t startFrame, int64_t endFrame)
{
    const auto oldStart = m_viewport.startFrame();
    const auto oldEnd = m_viewport.endFrame();
    m_viewport.setRange(startFrame, endFrame);
    if (oldStart != m_viewport.startFrame() || oldEnd != m_viewport.endFrame())
        emit viewportChanged(m_viewport.startFrame(), m_viewport.endFrame());
}

void TimelineModel::ensureFrameVisible(int64_t frame)
{
    const auto oldStart = m_viewport.startFrame();
    const auto oldEnd = m_viewport.endFrame();
    m_viewport.ensureVisible(frame);
    if (oldStart != m_viewport.startFrame() || oldEnd != m_viewport.endFrame())
        emit viewportChanged(m_viewport.startFrame(), m_viewport.endFrame());
}

void TimelineModel::setFrameRate(media::FrameRate rate)
{
    if (m_frameRate == rate) {
        return;
    }
    m_frameRate = rate;
    emit frameRateChanged(m_frameRate);
}

void TimelineModel::setCurrentFrame(int64_t frame)
{
    int64_t target = frame;

    if (m_frameCount <= 0) {
        target = 0;
    } else {
        target = std::clamp<int64_t>(target, effectiveStartFrame(), effectiveEndFrame());
    }

    if (m_currentFrame == target) {
        return;
    }
    m_currentFrame = target;
    emit currentFrameChanged(m_currentFrame);
}

void TimelineModel::setPlaybackRange(const PlaybackRange& range)
{
    PlaybackRange normalised = range;
    if (normalised.endFrame < normalised.startFrame) {
        std::swap(normalised.startFrame, normalised.endFrame);
    }
    normalised.startFrame = std::max<int64_t>(normalised.startFrame, 0);
    normalised.endFrame = std::max<int64_t>(normalised.endFrame, normalised.startFrame);
    if (m_frameCount > 0) {
        normalised.startFrame = std::min(normalised.startFrame, lastFrame());
        normalised.endFrame = std::min(normalised.endFrame, lastFrame());
    }

    if (m_range == normalised) {
        return;
    }
    m_range = normalised;
    emit playbackRangeChanged(m_range);

    // Pull the playhead back inside the new range.
    setCurrentFrame(m_currentFrame);
}

void TimelineModel::setRangeInAtCurrentFrame()
{
    PlaybackRange range = m_range;
    range.startFrame = m_currentFrame;
    if (!range.enabled) {
        // First I press with no range yet: run from here to the end.
        range.endFrame = std::max<int64_t>(lastFrame(), m_currentFrame);
        range.enabled = true;
    } else if (range.endFrame < range.startFrame) {
        range.endFrame = range.startFrame;
    }
    setPlaybackRange(range);
}

void TimelineModel::setRangeOutAtCurrentFrame()
{
    PlaybackRange range = m_range;
    range.endFrame = m_currentFrame;
    if (!range.enabled) {
        range.startFrame = 0;
        range.enabled = true;
    } else if (range.startFrame > range.endFrame) {
        range.startFrame = range.endFrame;
    }
    setPlaybackRange(range);
}

void TimelineModel::clearPlaybackRange()
{
    setPlaybackRange(PlaybackRange{});
}

int64_t TimelineModel::effectiveStartFrame() const
{
    if (m_range.enabled && m_range.isValid()) {
        return std::min(m_range.startFrame, std::max<int64_t>(lastFrame(), 0));
    }
    return 0;
}

int64_t TimelineModel::effectiveEndFrame() const
{
    const int64_t last = std::max<int64_t>(lastFrame(), 0);
    if (m_range.enabled && m_range.isValid()) {
        return std::min(m_range.endFrame, last);
    }
    return last;
}

void TimelineModel::addBookmark(const Bookmark& bookmark)
{
    const int64_t frame = std::clamp<int64_t>(bookmark.frame, 0, std::max<int64_t>(lastFrame(), 0));
    const auto existing = std::find_if(m_bookmarks.begin(), m_bookmarks.end(),
                                       [frame](const Bookmark& b) { return b.frame == frame; });
    // Add on an occupied frame selects the existing marker conceptually; it
    // never duplicates or silently replaces its stable ID/annotation.
    if (existing != m_bookmarks.end()) return;

    Bookmark stored = bookmark;
    stored.frame = frame;
    stored.mediaTimeUs = mediaTimeForFrame(stored.frame);
    if (stored.id == 0) stored.id = m_nextBookmarkId++;
    if (stored.name.isEmpty()) stored.name = QStringLiteral("Bookmark %1").arg(stored.id);
    m_bookmarks.push_back(stored);
    sortBookmarks();
    qCInfo(log::timeline) << "Bookmark added at frame" << bookmark.frame;
    emit bookmarksChanged();
}

void TimelineModel::removeBookmark(quint64 id)
{
    const auto it = std::find_if(m_bookmarks.begin(), m_bookmarks.end(),
                                 [id](const Bookmark& b) { return b.id == id; });
    if (it == m_bookmarks.end()) return;
    m_bookmarks.erase(it);
    emit bookmarksChanged();
}

void TimelineModel::removeBookmarkAt(int64_t frame)
{
    const auto it = std::find_if(m_bookmarks.begin(), m_bookmarks.end(),
                                 [frame](const Bookmark& b) { return b.frame == frame; });
    if (it == m_bookmarks.end()) {
        return;
    }
    m_bookmarks.erase(it);
    emit bookmarksChanged();
}

void TimelineModel::clearBookmarks()
{
    if (m_bookmarks.isEmpty()) {
        return;
    }
    m_bookmarks.clear();
    emit bookmarksChanged();
}

const Bookmark* TimelineModel::bookmarkAt(int64_t frame) const
{
    const auto it = std::find_if(m_bookmarks.cbegin(), m_bookmarks.cend(),
                                 [frame](const Bookmark& b) { return b.frame == frame; });
    return it == m_bookmarks.cend() ? nullptr : &(*it);
}

int64_t TimelineModel::nextBookmarkFrame(int64_t frame) const
{
    const auto it = std::find_if(m_bookmarks.cbegin(), m_bookmarks.cend(),
                                 [frame](const Bookmark& b) { return b.frame > frame; });
    return it == m_bookmarks.cend() ? (m_bookmarks.isEmpty() ? -1 : m_bookmarks.front().frame)
                                    : it->frame;
}

int64_t TimelineModel::previousBookmarkFrame(int64_t frame) const
{
    const auto it = std::find_if(m_bookmarks.crbegin(), m_bookmarks.crend(),
                                 [frame](const Bookmark& b) { return b.frame < frame; });
    return it == m_bookmarks.crend() ? (m_bookmarks.isEmpty() ? -1 : m_bookmarks.back().frame)
                                     : it->frame;
}

int64_t TimelineModel::mediaTimeForFrame(int64_t frame) const
{
    if (!m_frameRate.isValid()) return 0;
    const long double us = static_cast<long double>(std::max<int64_t>(0, frame))
        * 1'000'000.0L * m_frameRate.denominator / m_frameRate.numerator;
    return static_cast<int64_t>(us);
}

void TimelineModel::setPlaceholderExtent(int64_t frameCount, media::FrameRate rate)
{
    setFrameCount(frameCount);
    setFrameRate(rate);
    setCurrentFrame(0);
    setPlaceholder(true);

    qCInfo(log::timeline).noquote()
        << "Placeholder timeline installed:" << frameCount << "frames at"
        << rate.toDouble() << "fps -- no media is loaded";
}

void TimelineModel::setPlaceholder(bool placeholder)
{
    if (m_placeholder == placeholder) {
        return;
    }
    m_placeholder = placeholder;
    emit placeholderChanged(m_placeholder);
}

void TimelineModel::reset()
{
    setPlaceholder(false);
    clearBookmarks();
    clearPlaybackRange();
    setFrameCount(0);
    setCurrentFrame(0);
    setFrameRate(media::FrameRate{});
    m_nextBookmarkId = 1;
}

void TimelineModel::sortBookmarks()
{
    std::sort(m_bookmarks.begin(), m_bookmarks.end(),
              [](const Bookmark& a, const Bookmark& b) { return a.frame < b.frame; });
}

} // namespace atk::timeline
