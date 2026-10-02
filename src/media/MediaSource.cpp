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

} // namespace atk::media
