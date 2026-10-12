#include "media/MediaSource.h"

#include <QFileInfo>

#include <utility>

namespace atk::media {

MediaSource::MediaSource(QString filePath)
    : m_filePath(std::move(filePath))
{
    m_metadata.filePath = m_filePath;
    m_metadata.fileName = QFileInfo(m_filePath).fileName();
}

QString MediaSource::displayName() const
{
    if (isImageSequence()) {
        return sequenceDisplayName(m_filePath, m_imageOptions.sequenceFirst,
                                   m_imageOptions.sequenceLast);
    }
    return QFileInfo(m_filePath).fileName();
}

const QStringList& videoFileExtensions()
{
    static const QStringList extensions{
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("avi"),
        QStringLiteral("mkv"), QStringLiteral("m4v"), QStringLiteral("webm"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("wmv"),
        QStringLiteral("flv"),
    };
    return extensions;
}

bool isSupportedMediaPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return videoFileExtensions().contains(suffix) || isStillImagePath(path);
}

} // namespace atk::media
