#pragma once

#include "media/MediaMetadata.h"
#include "timeline/Bookmark.h"
#include "timeline/PlaybackRange.h"
#include "timeline/TimelineViewport.h"

#include <QObject>
#include <QVector>

#include <cstdint>

namespace atk::timeline {

/// The state of the timeline: extent, playhead, in/out range and bookmarks.
///
/// This is a pure model. It owns no widgets and performs no decoding; the
/// timeline widget renders it and PlaybackController advances it. Keeping the
/// playhead here rather than in the widget is what lets the viewer, the
/// timeline, the status bar and the external API all observe one value.
class TimelineModel : public QObject {
    Q_OBJECT

public:
    explicit TimelineModel(QObject* parent = nullptr);

    // --- Extent -----------------------------------------------------------
    /// Total frames in the source. Zero means "no media loaded".
    int64_t frameCount() const { return m_frameCount; }
    void setFrameCount(int64_t count);

    /// Last valid frame index, or -1 when empty.
    int64_t lastFrame() const { return m_frameCount > 0 ? m_frameCount - 1 : -1; }

    media::FrameRate frameRate() const { return m_frameRate; }
    void setFrameRate(media::FrameRate rate);

    // --- Playhead ---------------------------------------------------------
    int64_t currentFrame() const { return m_currentFrame; }
    /// Clamps to the source extent. Changing the review range alone must not
    /// move a stopped playhead.
    void setCurrentFrame(int64_t frame);

    // --- Range ------------------------------------------------------------
    const PlaybackRange& playbackRange() const { return m_range; }
    void setPlaybackRange(const PlaybackRange& range);
    /// Sets the in point to the current frame, extending the out point if needed.
    void setRangeInAtCurrentFrame();
    /// Sets the out point to the current frame, pulling the in point back if needed.
    void setRangeOutAtCurrentFrame();
    void clearPlaybackRange();

    const TimelineViewport& viewport() const { return m_viewport; }
    void fitViewport();
    void zoomViewport(double factor, int64_t anchorFrame);
    void panViewport(int64_t deltaFrames);
    void setViewportRange(int64_t startFrame, int64_t endFrame);
    void ensureFrameVisible(int64_t frame);

    /// First and last frame playback should visit. The visible viewport is the
    /// single active review range.
    int64_t effectiveStartFrame() const;
    int64_t effectiveEndFrame() const;

    // --- Bookmarks --------------------------------------------------------
    const QVector<Bookmark>& bookmarks() const { return m_bookmarks; }
    /// Adds a bookmark, replacing any existing one on the same frame.
    void addBookmark(const Bookmark& bookmark);
    void removeBookmarkAt(int64_t frame);
    void removeBookmark(quint64 id);
    void clearBookmarks();
    /// Returns nullptr when the frame carries no bookmark.
    const Bookmark* bookmarkAt(int64_t frame) const;
    /// Nearest bookmark strictly after/before `frame`, or -1 when there is none.
    int64_t nextBookmarkFrame(int64_t frame) const;
    int64_t previousBookmarkFrame(int64_t frame) const;
    int64_t mediaTimeForFrame(int64_t frame) const;

    // --- Placeholder state ------------------------------------------------
    /// True when the extent describes no real media.
    ///
    /// Phase 0 has no decoder, so the window installs a placeholder extent at
    /// startup to make the transport demonstrable. Every readout that shows a
    /// frame number must mark itself accordingly: the application must never
    /// look as though a file is open when none is.
    ///
    /// Loading real media clears the flag; see PlaybackController::setSource().
    bool isPlaceholder() const { return m_placeholder; }

    /// Installs a placeholder extent and marks the model as not holding media.
    void setPlaceholderExtent(int64_t frameCount, media::FrameRate rate);

    /// Resets extent, playhead, range and bookmarks. Used when media closes.
    void reset();

signals:
    // Signals use qint64 rather than int64_t because moc records the written
    // type name; qint64 is a type Qt has already registered, which keeps
    // queued connections working if a subsystem later moves off the UI thread.
    void currentFrameChanged(qint64 frame);
    void frameCountChanged(qint64 count);
    void frameRateChanged(atk::media::FrameRate rate);
    void playbackRangeChanged(atk::timeline::PlaybackRange range);
    void bookmarksChanged();
    void placeholderChanged(bool placeholder);
    void viewportChanged(qint64 startFrame, qint64 endFrame);

private:
    /// Keeps bookmarks sorted by frame so the next/previous lookups stay simple.
    void sortBookmarks();

    void setPlaceholder(bool placeholder);

    int64_t m_frameCount = 0;
    int64_t m_currentFrame = 0;
    media::FrameRate m_frameRate;
    PlaybackRange m_range;
    TimelineViewport m_viewport;
    QVector<Bookmark> m_bookmarks;
    quint64 m_nextBookmarkId = 1;
    bool m_placeholder = false;
};

} // namespace atk::timeline

// Registered so the model's signals can cross threads unchanged in later
// milestones.
Q_DECLARE_METATYPE(atk::media::FrameRate)
Q_DECLARE_METATYPE(atk::timeline::PlaybackRange)
