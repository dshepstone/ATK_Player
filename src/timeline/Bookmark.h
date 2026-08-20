#pragma once

#include <QColor>
#include <QString>

#include <cstdint>

namespace atk::timeline {

/// A marked frame, used for review notes.
///
/// Name, note and colour are all optional: pressing B during review drops a
/// bare frame marker, and the reviewer fills in the rest afterwards. Colour is
/// stored as a palette index rather than an RGB value so a future theme change
/// restyles existing bookmarks instead of stranding them on hard-coded colours.
struct Bookmark {
    /// Sentinel for "no colour assigned"; the UI draws the default marker.
    static constexpr int kNoColor = -1;

    int64_t frame = 0;
    QString name;
    QString note;
    int colorIndex = kNoColor;

    bool hasName() const { return !name.isEmpty(); }
    bool hasNote() const { return !note.isEmpty(); }
    bool hasColor() const { return colorIndex != kNoColor; }

    /// Label for the timeline tooltip: the name if set, otherwise the frame.
    QString displayLabel() const;

    friend bool operator==(const Bookmark&, const Bookmark&) = default;
};

/// Fixed palette bookmark colours index into. Out-of-range indices, including
/// kNoColor, return an invalid QColor.
QColor bookmarkColor(int colorIndex);
int bookmarkColorCount();

} // namespace atk::timeline
