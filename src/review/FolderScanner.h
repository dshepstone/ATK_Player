#pragma once

#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>

namespace atk::review {

/// One clip found in a review folder.
struct FoundClip {
    /// The file, or for an image sequence its pattern ("shot.%04d.png").
    QString path;
    QString displayName;      ///< file name, or "shot.[0001-0096].png"
    QString relativePath;     ///< from the scanned folder, for sorting and display
    bool sequence = false;
    int64_t sequenceFirst = -1;
    int64_t sequenceLast = -1;
    qint64 modifiedMsUtc = 0; ///< of the file (a sequence: its first frame)
};

/// Natural order: "clip2" before "clip10", case-insensitive, by relative path.
bool naturalLess(const QString& a, const QString& b);

/// Lists the clips in `folder` (and its subfolders when asked), in natural
/// order. Videos are one clip each. Numbered still images that form a
/// sequence become one clip; a lone numbered still stays a still. Hidden
/// files and folders are skipped.
///
/// Pure file-system work with no decoding, safe on a worker thread.
/// `isCancelled` is polled between directory entries.
QVector<FoundClip> scanFolder(const QString& folder, bool includeSubfolders,
                              const std::function<bool()>& isCancelled = {});

} // namespace atk::review
