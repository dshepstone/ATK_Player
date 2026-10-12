#pragma once

#include "media/ImageSource.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUuid>

namespace atk::media {

/// Makes the small picture shown for each clip in the playlist.
///
/// Lives on its own thread (the owner moves it there and calls render()
/// through a queued call) and opens one short-lived MediaDecoder per request,
/// so no decoder state is shared with playback and the GUI thread never
/// decodes. The result comes back through thumbnailReady().
///
/// Thumbnails are kept on disk in `cacheDirectory`, keyed by the file's path,
/// size and modification time, so reopening a playlist shows them at once and
/// a re-rendered clip gets a fresh one. Image sequences, which have no single
/// file to key on, are decoded every time.
class ThumbnailWorker : public QObject {
    Q_OBJECT
public:
    /// Bump when the frame choice or scaling changes, so old cached pictures
    /// are not reused.
    static constexpr int kCacheVersion = 1;

    explicit ThumbnailWorker(QObject* parent = nullptr);

    /// The frame used for a clip of `frameCount` frames: a little way in, so a
    /// black first frame or slate does not stand for the whole clip.
    static int64_t representativeFrame(int64_t frameCount);

    /// The cache file for `path` at `maxSize`, or empty when the path cannot
    /// be keyed (missing file, image-sequence pattern).
    static QString cacheFilePath(const QString& cacheDirectory, const QString& path, const QSize& maxSize);

public slots:
    void render(const QUuid& sourceId, const QString& path, quint64 token,
                const atk::media::ImageSourceOptions& still, const QSize& maxSize,
                const QString& cacheDirectory);

signals:
    /// `image` is null when the clip could not be read; `error` then says why.
    void thumbnailReady(QUuid sourceId, quint64 token, QImage image, QString error, bool fromCache);
};

} // namespace atk::media
