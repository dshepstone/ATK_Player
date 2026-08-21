#pragma once

#include <QColor>
#include <QString>

#include <cstdint>
#include <QtGlobal>

namespace atk::timeline {

enum class BookmarkType { Point, Range };

/// A marked frame, used for review notes.
///
/// Name, note and colour are all optional: pressing B during review drops a
/// bare frame marker, and the reviewer fills in the rest afterwards. Colour is
/// stored as a palette index rather than an RGB value so a future theme change
/// restyles existing bookmarks instead of stranding them on hard-coded colours.
struct Bookmark {
    /// Sentinel for "no colour assigned"; the UI draws the default marker.
    static constexpr int kNoColor = -1;

    quint64 id = 0;
    BookmarkType type = BookmarkType::Point;
    /// Zero-based inclusive start. Kept as `frame` for API compatibility.
    int64_t frame = 0;
    /// Zero-based inclusive end. Equals frame for point bookmarks.
    int64_t endFrame = 0;
    int64_t mediaTimeUs = 0;
    QString name;
    QString note;
    int colorIndex = kNoColor;

    bool hasName() const { return !name.isEmpty(); }
    bool hasNote() const { return !note.isEmpty(); }
    bool hasColor() const { return colorIndex != kNoColor; }
    bool isRange() const { return type == BookmarkType::Range && endFrame > frame; }

    /// Label for the timeline tooltip: the name if set, otherwise the frame.
    QString displayLabel() const;
    QString frameLabel() const;

    friend bool operator==(const Bookmark&, const Bookmark&) = default;
};

/// Fixed palette bookmark colours index into. Out-of-range indices, including
/// kNoColor, return an invalid QColor.
QColor bookmarkColor(int colorIndex);
int bookmarkColorCount();

} // namespace atk::timeline
