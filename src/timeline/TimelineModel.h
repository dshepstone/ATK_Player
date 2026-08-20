#pragma once

#include "media/MediaMetadata.h"
#include "timeline/Bookmark.h"
#include "timeline/PlaybackRange.h"

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
    /// Clamps to the active range, or to [0, lastFrame] when no range is set.
    void setCurrentFrame(int64_t frame);

    // --- Range ------------------------------------------------------------
    const PlaybackRange& playbackRange() const { return m_range; }
    void setPlaybackRange(const PlaybackRange& range);
    /// Sets the in point to the current frame, extending the out point if needed.
    void setRangeInAtCurrentFrame();
    /// Sets the out point to the current frame, pulling the in point back if needed.
    void setRangeOutAtCurrentFrame();
    void clearPlaybackRange();

    /// First and last frame playback should visit, honouring the range when
    /// enabled and the full extent otherwise.
    int64_t effectiveStartFrame() const;
    int64_t effectiveEndFrame() const;

    // --- Bookmarks --------------------------------------------------------
    const QVector<Bookmark>& bookmarks() const { return m_bookmarks; }
    /// Adds a bookmark, replacing any existing one on the same frame.
    void addBookmark(const Bookmark& bookmark);
    void removeBookmarkAt(int64_t frame);
    void clearBookmarks();
    /// Returns nullptr when the frame carries no bookmark.
    const Bookmark* bookmarkAt(int64_t frame) const;
    /// Nearest bookmark strictly after/before `frame`, or -1 when there is none.
    int64_t nextBookmarkFrame(int64_t frame) const;
    int64_t previousBookmarkFrame(int64_t frame) const;

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

private:
    /// Keeps bookmarks sorted by frame so the next/previous lookups stay simple.
    void sortBookmarks();

    int64_t m_frameCount = 0;
    int64_t m_currentFrame = 0;
    media::FrameRate m_frameRate;
    PlaybackRange m_range;
    QVector<Bookmark> m_bookmarks;
};

} // namespace atk::timeline

// Registered so the model's signals can cross threads unchanged in later
// milestones.
Q_DECLARE_METATYPE(atk::media::FrameRate)
Q_DECLARE_METATYPE(atk::timeline::PlaybackRange)
