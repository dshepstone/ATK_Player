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
    return QFileInfo(m_filePath).fileName();
}

} // namespace atk::media
