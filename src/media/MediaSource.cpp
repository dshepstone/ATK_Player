#include "media/MediaSource.h"

#include "core/Logging.h"

#include <QFileInfo>

#include <utility>

namespace atk::media {

MediaSource::MediaSource(QString filePath)
    : m_filePath(std::move(filePath))
    , m_decoder(createDecoderForFile(m_filePath))
{
    m_emptyMetadata.filePath = m_filePath;
}

MediaSource::~MediaSource() = default;

QString MediaSource::displayName() const
{
    return QFileInfo(m_filePath).fileName();
}

bool MediaSource::open()
{
    qCInfo(log::media).noquote() << "Opening media source:" << m_filePath;
    if (!m_decoder) {
        return false;
    }
    return m_decoder->open(m_filePath);
}

void MediaSource::close()
{
    if (m_decoder) {
        m_decoder->close();
    }
}

bool MediaSource::isOpen() const
{
    return m_decoder && m_decoder->isOpen();
}

const MediaMetadata& MediaSource::metadata() const
{
    return isOpen() ? m_decoder->metadata() : m_emptyMetadata;
}

QString MediaSource::lastError() const
{
    return m_decoder ? m_decoder->lastError() : QStringLiteral("No decoder available.");
}

bool MediaSource::frameAt(int64_t masterFrame, VideoFrame& out)
{
    if (!m_decoder) {
        return false;
    }
    const int64_t local = masterFrame + m_frameOffset;
    if (local < 0) {
        out = VideoFrame{};
        return false;
    }
    return m_decoder->decodeFrame(local, out);
}

} // namespace atk::media
