#include "media/decoders/FFmpegDecoder.h"

#include "core/Logging.h"

namespace atk::media {

FFmpegDecoder::FFmpegDecoder() = default;
FFmpegDecoder::~FFmpegDecoder() = default;

bool FFmpegDecoder::open(const QString& filePath)
{
    // TODO(M1): replace with a real avformat_open_input() sequence.
    m_metadata = MediaMetadata{};
    m_metadata.filePath = filePath;
    m_open = false;
    m_lastError = QStringLiteral("FFmpeg decoding is not implemented yet (planned for milestone M1).");
    qCWarning(log::media).noquote() << "Cannot open" << filePath << "--" << m_lastError;
    return false;
}

void FFmpegDecoder::close()
{
    m_open = false;
    m_metadata = MediaMetadata{};
}

bool FFmpegDecoder::isOpen() const
{
    return m_open;
}

const MediaMetadata& FFmpegDecoder::metadata() const
{
    return m_metadata;
}

bool FFmpegDecoder::decodeFrame(int64_t frameNumber, VideoFrame& out)
{
    // TODO(M1): seek + decode. Until then the caller always sees a miss, which
    // the viewer renders as its empty state.
    Q_UNUSED(frameNumber);
    out = VideoFrame{};
    m_lastError = QStringLiteral("FFmpeg decoding is not implemented yet (planned for milestone M1).");
    return false;
}

QString FFmpegDecoder::lastError() const
{
    return m_lastError;
}

std::unique_ptr<IDecoder> createDecoderForFile(const QString& filePath)
{
    // TODO(M1): dispatch on extension/probe once image-sequence support exists.
    Q_UNUSED(filePath);
    return std::make_unique<FFmpegDecoder>();
}

} // namespace atk::media
