#include "media/ThumbnailWorker.h"

#include "media/MediaDecoder.h"
#include "media/VideoFrame.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace atk::media {

ThumbnailWorker::ThumbnailWorker(QObject* parent) : QObject(parent) {}

int64_t ThumbnailWorker::representativeFrame(int64_t frameCount)
{
    if (frameCount <= 1) return 0;
    // 10% in, but no more than 240 frames in (and never past the end), so a
    // long clip still shows its opening shot.
    return std::clamp<int64_t>(frameCount / 10, 0, std::min<int64_t>(frameCount - 1, 240));
}

QString ThumbnailWorker::cacheFilePath(const QString& cacheDirectory, const QString& path, const QSize& maxSize)
{
    if (cacheDirectory.isEmpty()) return {};
    const QFileInfo info(path);
    if (!info.isFile()) return {};
    const QByteArray key = QStringLiteral("%1|%2|%3|%4x%5|v%6")
                               .arg(info.absoluteFilePath())
                               .arg(info.size())
                               .arg(info.lastModified().toMSecsSinceEpoch())
                               .arg(maxSize.width())
                               .arg(maxSize.height())
                               .arg(kCacheVersion)
                               .toUtf8();
    const QString name = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return QDir(cacheDirectory).filePath(name + QStringLiteral(".png"));
}

void ThumbnailWorker::render(const QUuid& sourceId, const QString& path, quint64 token,
                             const ImageSourceOptions& still, const QSize& maxSize,
                             const QString& cacheDirectory)
{
    const QString cached = cacheFilePath(cacheDirectory, path, maxSize);
    if (!cached.isEmpty()) {
        QImage image(cached);
        if (!image.isNull()) {
            emit thumbnailReady(sourceId, token, image, {}, true);
            return;
        }
    }

    if (!imageSourceExists(path, still)) {
        emit thumbnailReady(sourceId, token, {}, QStringLiteral("Media file is missing."), false);
        return;
    }
    MediaDecoder decoder;
    QString error;
    if (!decoder.open(path, &error, still)) {
        emit thumbnailReady(sourceId, token, {}, error, false);
        return;
    }
    VideoFrame frame;
    const int64_t index = representativeFrame(decoder.metadata().effectiveFrameCount());
    if (!decoder.frameAtIndex(index, frame, &error) || frame.image.isNull()) {
        // Some streams cannot seek precisely; their first frame is better than nothing.
        if (index == 0 || !decoder.frameAtIndex(0, frame, &error) || frame.image.isNull()) {
            emit thumbnailReady(sourceId, token, {}, error.isEmpty() ? QStringLiteral("No picture.") : error, false);
            return;
        }
    }
    decoder.close();

    QImage image = frame.image.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGB32);
    if (!cached.isEmpty() && QDir().mkpath(cacheDirectory)) {
        QSaveFile file(cached);
        if (file.open(QIODevice::WriteOnly) && image.save(&file, "PNG")) file.commit();
    }
    emit thumbnailReady(sourceId, token, image, {}, false);
}

} // namespace atk::media
