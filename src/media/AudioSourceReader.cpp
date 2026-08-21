#include "media/AudioSourceReader.h"

#include "core/Logging.h"
#include "media/ffmpeg/FFmpegUtil.h"

#include <QFileInfo>

extern "C" {
#include <libavutil/opt.h>
}

#include <algorithm>
#include <utility>

namespace atk::media {
namespace {

using namespace atk::media::ffmpeg;

constexpr AVSampleFormat kOutputSampleFormat = AV_SAMPLE_FMT_S16;

/// Upper bound on packets discarded while homing in on a requested position,
/// so a container with unhelpful timestamps cannot spin forever.
constexpr int kMaxSkipChunks = 4000;

} // namespace

AudioSourceReader::AudioSourceReader() = default;

AudioSourceReader::~AudioSourceReader()
{
    close();
}

bool AudioSourceReader::open(const QString& filePath, const AudioFormat& format, QString* error)
{
    close();

    if (!format.isValid()) {
        if (error) {
            *error = QStringLiteral("Invalid audio format requested.");
        }
        return false;
    }
    if (!QFileInfo::exists(filePath)) {
        if (error) {
            *error = QStringLiteral("File not found: %1").arg(filePath);
        }
        return false;
    }

    m_format = format;

    AVFormatContext* raw = nullptr;
    int result = avformat_open_input(&raw, filePath.toUtf8().constData(), nullptr, nullptr);
    if (result < 0) {
        if (error) {
            *error = ffmpeg::errorString("avformat_open_input", result);
        }
        return false;
    }
    m_format_ctx.reset(raw);

    result = avformat_find_stream_info(m_format_ctx.get(), nullptr);
    if (result < 0) {
        if (error) {
            *error = ffmpeg::errorString("avformat_find_stream_info", result);
        }
        close();
        return false;
    }

    if (!openStream(error) || !configureResampler(error)) {
        close();
        return false;
    }

    m_packet = makePacket();
    m_frame = makeFrame();
    if (!m_packet || !m_frame) {
        if (error) {
            *error = QStringLiteral("Out of memory preparing the audio reader.");
        }
        close();
        return false;
    }

    m_open = true;
    m_endOfStream = false;
    return true;
}

bool AudioSourceReader::openStream(QString* error)
{
    m_streamIndex =
        av_find_best_stream(m_format_ctx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_streamIndex < 0) {
        if (error) {
            *error = QStringLiteral("The file has no audio stream.");
        }
        return false;
    }

    AVStream* stream = m_format_ctx->streams[m_streamIndex];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        if (error) {
            *error = QStringLiteral("No decoder available for this audio codec.");
        }
        return false;
    }

    m_codec.reset(avcodec_alloc_context3(codec));
    if (!m_codec) {
        return false;
    }
    if (avcodec_parameters_to_context(m_codec.get(), stream->codecpar) < 0) {
        return false;
    }
    m_codec->pkt_timebase = stream->time_base;

    const int result = avcodec_open2(m_codec.get(), codec, nullptr);
    if (result < 0) {
        if (error) {
            *error = ffmpeg::errorString("avcodec_open2", result);
        }
        return false;
    }

    m_startTimeUs = stream->start_time == AV_NOPTS_VALUE
        ? 0
        : ffmpeg::ptsToMicroseconds(stream->start_time, stream->time_base);

    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        m_durationUs = ffmpeg::ptsToMicroseconds(stream->duration, stream->time_base);
    } else if (m_format_ctx->duration != AV_NOPTS_VALUE && m_format_ctx->duration > 0) {
        m_durationUs = m_format_ctx->duration;
    }

    return true;
}

bool AudioSourceReader::configureResampler(QString* error)
{
    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, m_format.channelCount);

    SwrContext* raw = nullptr;
    const int result = swr_alloc_set_opts2(
        &raw,
        &outLayout, kOutputSampleFormat, m_format.sampleRate,
        &m_codec->ch_layout, m_codec->sample_fmt, m_codec->sample_rate,
        0, nullptr);

    av_channel_layout_uninit(&outLayout);

    if (result < 0 || raw == nullptr) {
        if (error) {
            *error = ffmpeg::errorString("swr_alloc_set_opts2", result);
        }
        return false;
    }
    m_resampler.reset(raw);

    const int initResult = swr_init(m_resampler.get());
    if (initResult < 0) {
        if (error) {
            *error = ffmpeg::errorString("swr_init", initResult);
        }
        m_resampler.reset();
        return false;
    }
    return true;
}

void AudioSourceReader::close()
{
    m_frame.reset();
    m_packet.reset();
    m_resampler.reset();
    m_codec.reset();
    m_format_ctx.reset();

    m_streamIndex = -1;
    m_durationUs = -1;
    m_startTimeUs = 0;
    m_open = false;
    m_endOfStream = false;
}

bool AudioSourceReader::seekToMicroseconds(int64_t mediaUs, QString* error)
{
    if (!m_open) {
        return false;
    }

    AVStream* stream = m_format_ctx->streams[m_streamIndex];

    // Media time is measured from the stream's own start, so the offset has to
    // go back on before asking the demuxer for a timestamp.
    const int64_t target = ffmpeg::microsecondsToPts(
        std::max<int64_t>(0, mediaUs) + m_startTimeUs, stream->time_base);

    const int result =
        av_seek_frame(m_format_ctx.get(), m_streamIndex, target, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        if (error) {
            *error = ffmpeg::errorString("av_seek_frame", result);
        }
        return false;
    }

    avcodec_flush_buffers(m_codec.get());
    // Drop samples the resampler is holding from before the seek.
    swr_init(m_resampler.get());
    m_endOfStream = false;
    return true;
}

bool AudioSourceReader::decodeNext(AudioChunk& out, bool& produced, bool& endOfStream,
                                   QString* error)
{
    produced = false;
    endOfStream = false;

    const int readResult = av_read_frame(m_format_ctx.get(), m_packet.get());
    if (readResult == AVERROR_EOF) {
        avcodec_send_packet(m_codec.get(), nullptr);
        endOfStream = true;
    } else if (readResult < 0) {
        if (error) {
            *error = ffmpeg::errorString("av_read_frame", readResult);
        }
        return false;
    }

    PacketUnrefGuard guard(m_packet.get());

    if (!endOfStream) {
        if (m_packet->stream_index != m_streamIndex) {
            return true; // another stream; nothing to do
        }
        const int sendResult = avcodec_send_packet(m_codec.get(), m_packet.get());
        if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
            // A damaged packet should not end the scan.
            return true;
        }
    }

    while (true) {
        const int receiveResult = avcodec_receive_frame(m_codec.get(), m_frame.get());
        if (receiveResult == AVERROR(EAGAIN)) {
            break;
        }
        if (receiveResult == AVERROR_EOF) {
            endOfStream = true;
            break;
        }
        if (receiveResult < 0) {
            if (error) {
                *error = ffmpeg::errorString("avcodec_receive_frame", receiveResult);
            }
            return false;
        }

        const int64_t delay = swr_get_delay(m_resampler.get(), m_codec->sample_rate);
        const int64_t maxOut = av_rescale_rnd(delay + m_frame->nb_samples,
                                              m_format.sampleRate,
                                              m_codec->sample_rate, AV_ROUND_UP);
        if (maxOut <= 0) {
            av_frame_unref(m_frame.get());
            continue;
        }

        QByteArray pcm(static_cast<qsizetype>(maxOut * m_format.bytesPerFrame()),
                       Qt::Uninitialized);
        auto* destination = reinterpret_cast<uint8_t*>(pcm.data());

        const int converted = swr_convert(m_resampler.get(), &destination,
                                          static_cast<int>(maxOut),
                                          const_cast<const uint8_t**>(m_frame->data),
                                          m_frame->nb_samples);
        if (converted > 0) {
            pcm.resize(static_cast<qsizetype>(converted) * m_format.bytesPerFrame());

            int64_t pts = m_frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) {
                pts = m_frame->pts;
            }

            AVStream* stream = m_format_ctx->streams[m_streamIndex];
            out.ptsUs = pts == AV_NOPTS_VALUE
                ? 0
                : ffmpeg::ptsToMicroseconds(pts, stream->time_base) - m_startTimeUs;
            out.pcm = std::move(pcm);
            produced = true;
        }

        av_frame_unref(m_frame.get());
        if (produced) {
            break;
        }
    }

    return true;
}

bool AudioSourceReader::readChunk(AudioChunk& out, QString* error)
{
    if (!m_open || m_endOfStream) {
        return false;
    }

    for (int attempt = 0; attempt < kMaxSkipChunks; ++attempt) {
        bool produced = false;
        bool endOfStream = false;
        if (!decodeNext(out, produced, endOfStream, error)) {
            return false;
        }
        if (produced) {
            return true;
        }
        if (endOfStream) {
            m_endOfStream = true;
            return false;
        }
    }
    return false;
}

bool AudioSourceReader::readRange(int64_t startUs, int64_t durationUs, QByteArray& pcm,
                                  int64_t* actualStartUs, QString* error)
{
    if (!m_open || durationUs <= 0) {
        return false;
    }

    if (!seekToMicroseconds(startUs, error)) {
        return false;
    }

    pcm.clear();
    const int64_t wantedBytes = m_format.microsecondsToBytes(durationUs);
    int64_t rangeStartUs = -1;

    for (int attempt = 0; attempt < kMaxSkipChunks && pcm.size() < wantedBytes; ++attempt) {
        AudioChunk chunk;
        if (!readChunk(chunk, error)) {
            break;
        }

        const int64_t chunkDurationUs = m_format.bytesToMicroseconds(chunk.pcm.size());
        const int64_t chunkEndUs = chunk.ptsUs + chunkDurationUs;

        // Seeking lands on a packet boundary at or before the request, so the
        // first chunk usually begins early. Trim the lead-in rather than
        // returning audio from the wrong moment -- for lip-sync review a grain
        // that starts 40 ms early is simply the wrong sound.
        if (chunkEndUs <= startUs) {
            continue;
        }

        int64_t offsetBytes = 0;
        if (chunk.ptsUs < startUs) {
            offsetBytes = m_format.microsecondsToBytes(startUs - chunk.ptsUs);
            offsetBytes = std::min<int64_t>(offsetBytes, chunk.pcm.size());
        }

        if (rangeStartUs < 0) {
            rangeStartUs = chunk.ptsUs + m_format.bytesToMicroseconds(offsetBytes);
        }

        const int64_t available = chunk.pcm.size() - offsetBytes;
        const int64_t take = std::min<int64_t>(available, wantedBytes - pcm.size());
        if (take > 0) {
            pcm.append(chunk.pcm.constData() + offsetBytes, static_cast<qsizetype>(take));
        }
    }

    if (actualStartUs != nullptr) {
        *actualStartUs = rangeStartUs < 0 ? startUs : rangeStartUs;
    }
    return !pcm.isEmpty();
}

bool AudioSourceReader::scan(const std::function<void(const AudioChunk&)>& sink,
                             const CancelPredicate& isCancelled, QString* error)
{
    if (!m_open) {
        return false;
    }

    if (!seekToMicroseconds(0, error)) {
        return false;
    }

    while (true) {
        if (isCancelled && isCancelled()) {
            qCDebug(log::media) << "Audio scan cancelled";
            return false;
        }

        AudioChunk chunk;
        if (!readChunk(chunk, error)) {
            break;
        }
        if (sink) {
            sink(chunk);
        }
    }
    return true;
}

} // namespace atk::media
