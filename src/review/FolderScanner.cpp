#include "review/FolderScanner.h"

#include "media/ImageSource.h"
#include "media/MediaSource.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace atk::review {

bool naturalLess(const QString& a, const QString& b)
{
    // Our own comparison rather than QCollator's numeric mode, which some
    // platforms' Qt builds lack: the order must be the same everywhere.
    // Runs of digits compare by value ("2" < "10"; "007" ties "7", then the
    // shorter run first); everything else compares case-insensitively.
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype ei = i, ej = j;
            while (ei < a.size() && a[ei].isDigit()) ++ei;
            while (ej < b.size() && b[ej].isDigit()) ++ej;
            // Compare by value without overflow: strip leading zeros, then by
            // length, then digit by digit.
            qsizetype si = i, sj = j;
            while (si + 1 < ei && a[si] == QLatin1Char('0')) ++si;
            while (sj + 1 < ej && b[sj] == QLatin1Char('0')) ++sj;
            if (ei - si != ej - sj) return ei - si < ej - sj;
            for (qsizetype k = 0; k < ei - si; ++k)
                if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k];
            if (ei - i != ej - j) return ei - i < ej - j;
            i = ei;
            j = ej;
            continue;
        }
        const QChar ca = a[i].toCaseFolded();
        const QChar cb = b[j].toCaseFolded();
        if (ca != cb) return ca < cb;
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j) return a.size() - i < b.size() - j;
    return a < b; // equal ignoring case: a stable, case-sensitive tie-break
}

QVector<FoundClip> scanFolder(const QString& folder, bool includeSubfolders, const std::function<bool()>& isCancelled)
{
    QVector<FoundClip> clips;
    const QDir root(folder);
    if (!root.exists()) return clips;

    QDirIterator it(root.absolutePath(), QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                    includeSubfolders ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    QSet<QString> sequencesSeen;
    while (it.hasNext()) {
        if (isCancelled && isCancelled()) return {};
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = root.relativeFilePath(path);
        // Hidden files, and anything inside a hidden folder (".thumbnails"...).
        if (info.isHidden() || relative.startsWith(QLatin1Char('.')) || relative.contains(QStringLiteral("/.")))
            continue;
        if (!media::isSupportedMediaPath(path)) continue;

        FoundClip clip;
        clip.path = info.absoluteFilePath();
        clip.displayName = info.fileName();
        clip.relativePath = relative;
        clip.modifiedMsUtc = info.lastModified().toMSecsSinceEpoch();
        if (media::isStillImagePath(path)) {
            if (const auto sequence = media::detectImageSequence(clip.path); sequence && sequence->length() > 1) {
                if (sequencesSeen.contains(sequence->pattern)) continue; // one clip per sequence
                sequencesSeen.insert(sequence->pattern);
                clip.path = sequence->pattern;
                clip.sequence = true;
                clip.sequenceFirst = sequence->first;
                clip.sequenceLast = sequence->last;
                clip.displayName = media::sequenceDisplayName(sequence->pattern, sequence->first, sequence->last);
                clip.relativePath = root.relativeFilePath(sequence->pattern);
            }
        }
        clips.append(std::move(clip));
    }
    std::sort(clips.begin(), clips.end(),
              [](const FoundClip& a, const FoundClip& b) { return naturalLess(a.relativePath, b.relativePath); });
    return clips;
}

} // namespace atk::review
