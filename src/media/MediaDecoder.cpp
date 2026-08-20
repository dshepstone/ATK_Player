#include "media/MediaDecoder.h"

#include "core/Logging.h"
#include "media/ffmpeg/FFmpegUtil.h"

#include <QFileInfo>

extern "C" {
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cmath>

namespace atk::media {
namespace {

using namespace atk::media::ffmpeg;

/// The pixel format frames are converted to for display.
///
/// BGRA matches QImage::Format_RGB32 byte-for-byte on little-endian, so the
/// QImage can wrap the scaler's output with no further conversion.
constexpr AVPixelFormat kDisplayPixelFormat = AV_PIX_FMT_BGRA;

/// Interleaved signed 16-bit is universally accepted by audio devices and is
/// what AudioFormat::bytesPerSample = 2 describes.
constexpr AVSampleFormat kOutputSampleFormat = AV_SAMPLE_FMT_S16;

/// Guard against a runaway decode when a seek target can never be reached.
constexpr int kMaxDecodeStepsPerSeek = 4000;

/// How far back to retry when a seek lands after the requested frame.
constexpr int kSeekRetryAttempts = 4;

/// Caps on decoded-but-unclaimed output. Both streams come from one packet
/// sequence, so asking for video also produces audio; these bound how far one
/// may run ahead of the other before it is treated as a decode error rather
/// than growing without limit.
constexpr std::size_t kMaxPendingVideo = 256;
constexpr std::size_t kMaxPendingAudio = 512;

/// A container-reported frame count is only believed when it is plausible.
bool frameCountLooksTrustworthy(int64_t reported, int64_t estimated)
{
    if (reported <= 0) {
        return false;
    }
    if (estimated <= 0) {
        return true;
    }
    // Some muxers write a wildly wrong nb_frames. Accept it only when it agrees
    // with duration x rate to within 5%.
    const double ratio = static_cast<double>(reported) / static_cast<double>(estimated);
    return ratio > 0.95 && ratio < 1.05;
}

} // namespace

MediaDecoder::MediaDecoder() = default;

MediaDecoder::~MediaDecoder()
{
    close();
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

bool MediaDecoder::open(const QString& filePath, QString* error)
{
    close();

    const QFileInfo info(filePath);
    if (!info.exists()) {
        if (error) {
            *error = QStringLiteral("File not found: %1").arg(filePath);
        }
        return false;
    }
    if (!info.isReadable()) {
        if (error) {
            *error = QStringLiteral("File cannot be read (check permissions): %1").arg(filePath);
        }
        return false;
    }

    AVFormatContext* raw = nullptr;
    int result = avformat_open_input(&raw, filePath.toUtf8().constData(), nullptr, nullptr);
    if (result < 0) {
        if (error) {
            *error = QStringLiteral("Could not open media: %1")
                         .arg(ffmpeg::errorString(result));
        }
        qCWarning(log::media).noquote()
            << ffmpeg::errorString("avformat_open_input", result) << filePath;
        return false;
    }
    m_format.reset(raw);

    result = avformat_find_stream_info(m_format.get(), nullptr);
    if (result < 0) {
        if (error) {
            *error = QStringLiteral("Could not read stream information: %1")
                         .arg(ffmpeg::errorString(result));
        }
        qCWarning(log::media).noquote()
            << ffmpeg::errorString("avformat_find_stream_info", result);
        close();
        return false;
    }

    m_metadata = MediaMetadata{};
    m_metadata.filePath = filePath;
    m_metadata.fileName = info.fileName();

    if (m_format->iformat != nullptr) {
        m_metadata.containerFormat = QString::fromUtf8(m_format->iformat->name);
        if (m_format->iformat->long_name != nullptr) {
            m_metadata.containerLongName = QString::fromUtf8(m_format->iformat->long_name);
        }
    }

    m_metadata.videoStreamIndex =
        av_find_best_stream(m_format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_metadata.audioStreamIndex =
        av_find_best_stream(m_format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (m_metadata.videoStreamIndex < 0 && m_metadata.audioStreamIndex < 0) {
        if (error) {
            *error = QStringLiteral("The file contains no playable video or audio stream.");
        }
        close();
        return false;
    }

    if (m_metadata.videoStreamIndex >= 0 && !openVideoStream(error)) {
        close();
        return false;
    }

    // Audio is optional: a file with no usable audio track is still reviewable,
    // so a failure here degrades to video-only rather than refusing the file.
    if (m_metadata.audioStreamIndex >= 0 && !openAudioStream(nullptr)) {
        qCWarning(log::media) << "Audio stream could not be opened; continuing without audio";
        m_metadata.hasAudio = false;
        m_metadata.audioStreamIndex = -1;
        m_audioCodec.reset();
    }

    if (!m_metadata.hasVideo) {
        if (error) {
            *error = QStringLiteral("The file has no video stream ATK Player can decode.");
        }
        close();
        return false;
    }

    readMetadata();

    m_packet = makePacket();
    m_frame = makeFrame();
    if (!m_packet || !m_frame) {
        if (error) {
            *error = QStringLiteral("Out of memory while preparing the decoder.");
        }
        close();
        return false;
    }

    m_open = true;
    resetStreamState();

    qCInfo(log::media).noquote()
        << "Opened" << m_metadata.fileName
        << "|" << m_metadata.containerFormat
        << "|" << m_metadata.videoCodecName
        << QStringLiteral("%1x%2").arg(m_metadata.resolution.width()).arg(m_metadata.resolution.height())
        << QStringLiteral("%1 fps").arg(m_metadata.frameRate.toDouble())
        << "| audio:" << (m_metadata.hasAudio ? m_metadata.audioCodecName : QStringLiteral("none"))
        << "| video stream" << m_metadata.videoStreamIndex
        << "| audio stream" << m_metadata.audioStreamIndex;

    return true;
}

bool MediaDecoder::openVideoStream(QString* error)
{
    AVStream* stream = m_format->streams[m_metadata.videoStreamIndex];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        if (error) {
            *error = QStringLiteral("No decoder available for this video codec.");
        }
        return false;
    }

    m_videoCodec.reset(avcodec_alloc_context3(codec));
    if (!m_videoCodec) {
        if (error) {
            *error = QStringLiteral("Out of memory allocating the video decoder.");
        }
        return false;
    }

    int result = avcodec_parameters_to_context(m_videoCodec.get(), stream->codecpar);
    if (result < 0) {
        if (error) {
            *error = QStringLiteral("Could not configure the video decoder: %1")
                         .arg(ffmpeg::errorString(result));
        }
        return false;
    }

    // Let FFmpeg use the machine's cores for decoding; software decode of 1080p
    // on one thread does not keep up with real-time playback.
    m_videoCodec->thread_count = 0;
    m_videoCodec->pkt_timebase = stream->time_base;

    result = avcodec_open2(m_videoCodec.get(), codec, nullptr);
    if (result < 0) {
        if (error) {
            *error = QStringLiteral("Could not open the video decoder: %1")
                         .arg(ffmpeg::errorString(result));
        }
        return false;
    }

    m_metadata.hasVideo = true;
    m_metadata.videoCodecName = QString::fromUtf8(codec->name);
    if (codec->long_name != nullptr) {
        m_metadata.videoCodecLongName = QString::fromUtf8(codec->long_name);
    }
    return true;
}

bool MediaDecoder::openAudioStream(QString* error)
{
    AVStream* stream = m_format->streams[m_metadata.audioStreamIndex];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        if (error) {
            *error = QStringLiteral("No decoder available for this audio codec.");
        }
        return false;
    }

    m_audioCodec.reset(avcodec_alloc_context3(codec));
    if (!m_audioCodec) {
        return false;
    }

    int result = avcodec_parameters_to_context(m_audioCodec.get(), stream->codecpar);
    if (result < 0) {
        return false;
    }
    m_audioCodec->pkt_timebase = stream->time_base;

    result = avcodec_open2(m_audioCodec.get(), codec, nullptr);
    if (result < 0) {
        return false;
    }

    m_metadata.hasAudio = true;
    m_metadata.audioCodecName = QString::fromUtf8(codec->name);
    if (codec->long_name != nullptr) {
        m_metadata.audioCodecLongName = QString::fromUtf8(codec->long_name);
    }
    return true;
}

void MediaDecoder::readMetadata()
{
    if (m_metadata.hasVideo) {
        AVStream* stream = m_format->streams[m_metadata.videoStreamIndex];

        m_metadata.resolution = QSize(m_videoCodec->width, m_videoCodec->height);
        m_metadata.videoTimeBase = { stream->time_base.num, stream->time_base.den };
        m_metadata.videoStartTime =
            stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;

        const char* pixelName = av_get_pix_fmt_name(m_videoCodec->pix_fmt);
        if (pixelName != nullptr) {
            m_metadata.pixelFormatName = QString::fromUtf8(pixelName);
        }

        if (m_videoCodec->sample_aspect_ratio.num > 0
            && m_videoCodec->sample_aspect_ratio.den > 0) {
            m_metadata.pixelAspectRatio =
                av_q2d(m_videoCodec->sample_aspect_ratio);
        }

        // av_guess_frame_rate combines the container's declared rate with the
        // codec's, which is what handles the many files that get one of the two
        // wrong.
        const AVRational guessed = av_guess_frame_rate(m_format.get(), stream, nullptr);
        if (ffmpeg::isValidRational(guessed)) {
            m_metadata.frameRate = { guessed.num, guessed.den };
        }

        // Display rotation, if the container carries one.
        for (int i = 0; i < stream->codecpar->nb_coded_side_data; ++i) {
            const AVPacketSideData& side = stream->codecpar->coded_side_data[i];
            if (side.type != AV_PKT_DATA_DISPLAYMATRIX || side.data == nullptr) {
                continue;
            }
            const double degrees =
                av_display_rotation_get(reinterpret_cast<const int32_t*>(side.data));
            if (!std::isnan(degrees)) {
                // av_display_rotation_get returns the rotation to undo, in
                // (-180, 180]. Normalise to a positive multiple of 90.
                int normalised = static_cast<int>(std::lround(-degrees)) % 360;
                if (normalised < 0) {
                    normalised += 360;
                }
                m_metadata.rotationDegrees = normalised;
            }
            break;
        }
    }

    if (m_metadata.hasAudio) {
        AVStream* stream = m_format->streams[m_metadata.audioStreamIndex];
        m_metadata.audioSampleRate = m_audioCodec->sample_rate;
        m_metadata.audioChannelCount = m_audioCodec->ch_layout.nb_channels;
        m_metadata.audioTimeBase = { stream->time_base.num, stream->time_base.den };

        char layout[128] = {};
        if (av_channel_layout_describe(&m_audioCodec->ch_layout, layout, sizeof(layout)) > 0) {
            m_metadata.audioChannelLayout = QString::fromUtf8(layout);
        }
    }

    // --- Duration ---------------------------------------------------------
    if (m_format->duration != AV_NOPTS_VALUE && m_format->duration > 0) {
        // AV_TIME_BASE is already microseconds.
        m_metadata.durationUs = m_format->duration;
    } else if (m_metadata.hasVideo) {
        AVStream* stream = m_format->streams[m_metadata.videoStreamIndex];
        if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
            m_metadata.durationUs =
                ffmpeg::ptsToMicroseconds(stream->duration, stream->time_base);
        }
    }

    // --- Frame count ------------------------------------------------------
    //
    // Priority: a trustworthy container count, then duration x rate. Which one
    // was used is recorded so the UI can mark an estimate as such rather than
    // presenting a guess as fact.
    const int64_t estimated =
        (m_metadata.durationUs > 0 && m_metadata.frameRate.isValid())
            ? ffmpeg::microsecondsToFrameIndex(
                  m_metadata.durationUs,
                  AVRational{ m_metadata.frameRate.numerator, m_metadata.frameRate.denominator })
            : -1;

    int64_t reported = -1;
    if (m_metadata.hasVideo) {
        reported = m_format->streams[m_metadata.videoStreamIndex]->nb_frames;
    }

    if (frameCountLooksTrustworthy(reported, estimated)) {
        m_metadata.frameCount = reported;
        m_metadata.frameCountSource = FrameCountSource::StreamMetadata;
    } else if (estimated > 0) {
        m_metadata.frameCount = estimated;
        m_metadata.frameCountSource = FrameCountSource::EstimatedFromDuration;
    } else {
        m_metadata.frameCount = -1;
        m_metadata.frameCountSource = FrameCountSource::Unknown;
    }

    qCDebug(log::media).noquote()
        << "Frame count" << m_metadata.frameCount
        << "source" << static_cast<int>(m_metadata.frameCountSource)
        << "(container reported" << reported << ", estimate" << estimated << ")";
}

void MediaDecoder::close()
{
    // Reverse acquisition order. The RAII types do the actual freeing; this
    // just makes the ordering explicit and drops the buffered output.
    m_pendingVideo.clear();
    m_pendingAudio.clear();

    m_frame.reset();
    m_packet.reset();
    m_resampler.reset();
    m_scaler.reset();
    m_audioCodec.reset();
    m_videoCodec.reset();
    m_format.reset();

    m_metadata = MediaMetadata{};
    m_outputAudioFormat = AudioFormat{};
    m_open = false;
    resetStreamState();
}

void MediaDecoder::resetStreamState()
{
    m_nextVideoFrameIndex = 0;
    m_nextAudioPtsUs = 0;
    m_demuxEof = false;
    m_videoEof = false;
    m_audioEof = false;
    m_draining = false;
}

// ---------------------------------------------------------------------------
// Packet pump
// ---------------------------------------------------------------------------

DecodeStatus MediaDecoder::pump(QString* error)
{
    if (!m_open) {
        if (error) {
            *error = QStringLiteral("No media is open.");
        }
        return DecodeStatus::Error;
    }

    if (m_demuxEof) {
        return drain(error);
    }

    const int result = av_read_frame(m_format.get(), m_packet.get());
    if (result == AVERROR_EOF) {
        m_demuxEof = true;
        // Tell both codecs no more packets are coming so they release whatever
        // they have buffered; B-frame reordering means the last frames only
        // appear during this flush.
        if (m_videoCodec) {
            avcodec_send_packet(m_videoCodec.get(), nullptr);
        }
        if (m_audioCodec) {
            avcodec_send_packet(m_audioCodec.get(), nullptr);
        }
        m_draining = true;
        return drain(error);
    }
    if (result < 0) {
        if (error) {
            *error = QStringLiteral("Error reading media: %1").arg(ffmpeg::errorString(result));
        }
        qCWarning(log::media).noquote() << ffmpeg::errorString("av_read_frame", result);
        return DecodeStatus::Error;
    }

    // Release the packet payload however this scope exits.
    PacketUnrefGuard guard(m_packet.get());

    AVCodecContext* target = nullptr;
    bool isVideo = false;
    if (m_videoCodec && m_packet->stream_index == m_metadata.videoStreamIndex) {
        target = m_videoCodec.get();
        isVideo = true;
    } else if (m_audioCodec && m_packet->stream_index == m_metadata.audioStreamIndex) {
        target = m_audioCodec.get();
    } else {
        // A stream we do not consume (subtitles, data). Skipping is normal.
        return DecodeStatus::Ok;
    }

    int sendResult = avcodec_send_packet(target, m_packet.get());
    if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
        // A damaged packet is not fatal: log it and keep going, so one bad
        // frame does not end playback of an otherwise usable file.
        qCDebug(log::media).noquote()
            << ffmpeg::errorString("avcodec_send_packet", sendResult);
        return DecodeStatus::Ok;
    }

    while (true) {
        const int receiveResult = avcodec_receive_frame(target, m_frame.get());
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
            break;
        }
        if (receiveResult < 0) {
            if (error) {
                *error = QStringLiteral("Decoding failed: %1")
                             .arg(ffmpeg::errorString(receiveResult));
            }
            return DecodeStatus::Error;
        }

        if (isVideo) {
            VideoFrame frame;
            if (convertVideoFrame(m_frame.get(), frame, error)) {
                m_pendingVideo.push_back(std::move(frame));
            }
        } else {
            convertAudioFrame(m_frame.get(), error);
        }
        av_frame_unref(m_frame.get());
    }

    if (m_pendingVideo.size() > kMaxPendingVideo || m_pendingAudio.size() > kMaxPendingAudio) {
        // One stream has run far ahead of the other, which means the caller is
        // not consuming both. Refusing here keeps memory bounded instead of
        // letting the queues grow until the process dies.
        qCWarning(log::media)
            << "Decoder output queues exceeded their bound; dropping buffered output";
        if (m_pendingVideo.size() > kMaxPendingVideo) {
            m_pendingVideo.erase(m_pendingVideo.begin(),
                                 m_pendingVideo.begin()
                                     + static_cast<long>(m_pendingVideo.size() - kMaxPendingVideo));
        }
        if (m_pendingAudio.size() > kMaxPendingAudio) {
            m_pendingAudio.erase(m_pendingAudio.begin(),
                                 m_pendingAudio.begin()
                                     + static_cast<long>(m_pendingAudio.size() - kMaxPendingAudio));
        }
    }

    return DecodeStatus::Ok;
}

DecodeStatus MediaDecoder::drain(QString* error)
{
    bool produced = false;

    if (m_videoCodec && !m_videoEof) {
        while (true) {
            const int result = avcodec_receive_frame(m_videoCodec.get(), m_frame.get());
            if (result == AVERROR_EOF) {
                m_videoEof = true;
                break;
            }
            if (result == AVERROR(EAGAIN)) {
                break;
            }
            if (result < 0) {
                if (error) {
                    *error = QStringLiteral("Decoding failed while draining: %1")
                                 .arg(ffmpeg::errorString(result));
                }
                return DecodeStatus::Error;
            }
            VideoFrame frame;
            if (convertVideoFrame(m_frame.get(), frame, error)) {
                m_pendingVideo.push_back(std::move(frame));
                produced = true;
            }
            av_frame_unref(m_frame.get());
        }
    }

    if (m_audioCodec && !m_audioEof) {
        while (true) {
            const int result = avcodec_receive_frame(m_audioCodec.get(), m_frame.get());
            if (result == AVERROR_EOF) {
                m_audioEof = true;
                break;
            }
            if (result == AVERROR(EAGAIN)) {
                break;
            }
            if (result < 0) {
                break;
            }
            if (convertAudioFrame(m_frame.get(), error)) {
                produced = true;
            }
            av_frame_unref(m_frame.get());
        }
    }

    if (produced) {
        return DecodeStatus::Ok;
    }
    if (m_videoCodec) {
        m_videoEof = true;
    }
    if (m_audioCodec) {
        m_audioEof = true;
    }
    return DecodeStatus::EndOfFile;
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

int64_t MediaDecoder::frameIndexForTimestamp(int64_t pts) const
{
    const AVRational rate{ m_metadata.frameRate.numerator, m_metadata.frameRate.denominator };
    const AVRational timeBase{ m_metadata.videoTimeBase.numerator,
                               m_metadata.videoTimeBase.denominator };
    return ffmpeg::ptsToFrameIndex(pts, rate, timeBase, m_metadata.videoStartTime);
}

bool MediaDecoder::convertVideoFrame(const AVFrame* source, VideoFrame& out, QString* error)
{
    const int width = source->width;
    const int height = source->height;
    if (width <= 0 || height <= 0) {
        return false;
    }

    // sws_getCachedContext reuses the existing scaler whenever the parameters
    // are unchanged, which is every frame of a normal file.
    m_scaler.reset(sws_getCachedContext(
        m_scaler.release(),
        width, height, static_cast<AVPixelFormat>(source->format),
        width, height, kDisplayPixelFormat,
        SWS_BILINEAR, nullptr, nullptr, nullptr));

    if (!m_scaler) {
        if (error) {
            *error = QStringLiteral("Could not create the pixel format converter.");
        }
        return false;
    }

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("Out of memory allocating a video frame.");
        }
        return false;
    }

    uint8_t* destinationData[4] = { image.bits(), nullptr, nullptr, nullptr };
    int destinationStride[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };

    const int converted = sws_scale(m_scaler.get(), source->data, source->linesize,
                                    0, height, destinationData, destinationStride);
    if (converted <= 0) {
        if (error) {
            *error = QStringLiteral("Pixel format conversion produced no output.");
        }
        return false;
    }

    // best_effort_timestamp is what makes display order correct: it accounts for
    // reordering, so B-frames land at their presentation position rather than
    // the order they arrived in.
    int64_t pts = source->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) {
        pts = source->pts;
    }

    const AVRational timeBase{ m_metadata.videoTimeBase.numerator,
                               m_metadata.videoTimeBase.denominator };

    out.image = std::move(image);
    out.width = width;
    out.height = height;
    out.ptsTicks = pts;

    if (pts == AV_NOPTS_VALUE) {
        // No usable timestamp: fall back to sequential numbering so the file
        // still plays, and derive the time from the index instead.
        out.frameIndex = m_nextVideoFrameIndex;
        out.ptsUs = ffmpeg::frameIndexToMicroseconds(
            out.frameIndex,
            AVRational{ m_metadata.frameRate.numerator, m_metadata.frameRate.denominator });
    } else {
        out.frameIndex = frameIndexForTimestamp(pts);
        out.ptsUs = ffmpeg::ptsToMicroseconds(pts - m_metadata.videoStartTime, timeBase);
    }

    m_nextVideoFrameIndex = out.frameIndex + 1;
    return true;
}

bool MediaDecoder::convertAudioFrame(const AVFrame* source, QString* error)
{
    if (!m_resampler || !m_outputAudioFormat.isValid()) {
        // Audio output has not been configured; nothing to convert into.
        return false;
    }

    // Allow for the resampler holding samples back between calls.
    const int64_t delay = swr_get_delay(m_resampler.get(), m_audioCodec->sample_rate);
    const int64_t maxOutSamples = av_rescale_rnd(delay + source->nb_samples,
                                                 m_outputAudioFormat.sampleRate,
                                                 m_audioCodec->sample_rate,
                                                 AV_ROUND_UP);
    if (maxOutSamples <= 0) {
        return false;
    }

    QByteArray pcm(static_cast<qsizetype>(maxOutSamples * m_outputAudioFormat.bytesPerFrame()),
                   Qt::Uninitialized);

    auto* destination = reinterpret_cast<uint8_t*>(pcm.data());
    const int produced = swr_convert(m_resampler.get(), &destination,
                                     static_cast<int>(maxOutSamples),
                                     const_cast<const uint8_t**>(source->data),
                                     source->nb_samples);
    if (produced < 0) {
        if (error) {
            *error = QStringLiteral("Audio resampling failed: %1")
                         .arg(ffmpeg::errorString(produced));
        }
        return false;
    }
    if (produced == 0) {
        return false;
    }

    pcm.resize(static_cast<qsizetype>(produced) * m_outputAudioFormat.bytesPerFrame());

    int64_t pts = source->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) {
        pts = source->pts;
    }

    AudioChunk chunk;
    if (pts == AV_NOPTS_VALUE) {
        chunk.ptsUs = m_nextAudioPtsUs;
    } else {
        AVStream* stream = m_format->streams[m_metadata.audioStreamIndex];
        const int64_t startTime =
            stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        chunk.ptsUs = ffmpeg::ptsToMicroseconds(pts - startTime, stream->time_base);
    }
    chunk.pcm = std::move(pcm);

    m_nextAudioPtsUs = chunk.ptsUs + m_outputAudioFormat.bytesToMicroseconds(chunk.pcm.size());
    m_pendingAudio.push_back(std::move(chunk));
    return true;
}

// ---------------------------------------------------------------------------
// Audio configuration
// ---------------------------------------------------------------------------

bool MediaDecoder::configureAudioOutput(const AudioFormat& format, QString* error)
{
    if (!m_open || !m_metadata.hasAudio || !m_audioCodec) {
        return false;
    }
    if (!format.isValid()) {
        if (error) {
            *error = QStringLiteral("Invalid audio output format requested.");
        }
        return false;
    }

    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, format.channelCount);

    SwrContext* raw = nullptr;
    const int result = swr_alloc_set_opts2(
        &raw,
        &outLayout, kOutputSampleFormat, format.sampleRate,
        &m_audioCodec->ch_layout, m_audioCodec->sample_fmt, m_audioCodec->sample_rate,
        0, nullptr);

    av_channel_layout_uninit(&outLayout);

    if (result < 0 || raw == nullptr) {
        if (error) {
            *error = QStringLiteral("Could not create the audio resampler: %1")
                         .arg(ffmpeg::errorString(result));
        }
        return false;
    }
    m_resampler.reset(raw);

    const int initResult = swr_init(m_resampler.get());
    if (initResult < 0) {
        if (error) {
            *error = QStringLiteral("Could not initialise the audio resampler: %1")
                         .arg(ffmpeg::errorString(initResult));
        }
        m_resampler.reset();
        return false;
    }

    m_outputAudioFormat = format;

    qCInfo(log::media).noquote()
        << "Audio resampling" << m_metadata.audioSampleRate << "Hz"
        << m_metadata.audioChannelCount << "ch ->"
        << format.sampleRate << "Hz" << format.channelCount << "ch s16";

    return true;
}

// ---------------------------------------------------------------------------
// Frame access
// ---------------------------------------------------------------------------

DecodeStatus MediaDecoder::nextVideoFrame(VideoFrame& out, QString* error)
{
    if (!m_open) {
        if (error) {
            *error = QStringLiteral("No media is open.");
        }
        return DecodeStatus::Error;
    }

    int steps = 0;
    while (m_pendingVideo.empty()) {
        if (m_videoEof) {
            return DecodeStatus::EndOfFile;
        }
        if (++steps > kMaxDecodeStepsPerSeek) {
            if (error) {
                *error = QStringLiteral("Decoder produced no video frame within the step limit.");
            }
            return DecodeStatus::Error;
        }
        const DecodeStatus status = pump(error);
        if (status == DecodeStatus::Error) {
            return status;
        }
        if (status == DecodeStatus::EndOfFile && m_pendingVideo.empty()) {
            return DecodeStatus::EndOfFile;
        }
    }

    out = std::move(m_pendingVideo.front());
    m_pendingVideo.pop_front();
    return DecodeStatus::Ok;
}

DecodeStatus MediaDecoder::nextAudioChunk(AudioChunk& out, QString* error)
{
    if (!m_open || !m_metadata.hasAudio) {
        return DecodeStatus::EndOfFile;
    }

    int steps = 0;
    while (m_pendingAudio.empty()) {
        if (m_audioEof) {
            return DecodeStatus::EndOfFile;
        }
        if (++steps > kMaxDecodeStepsPerSeek) {
            return DecodeStatus::EndOfFile;
        }
        const DecodeStatus status = pump(error);
        if (status == DecodeStatus::Error) {
            return status;
        }
        if (status == DecodeStatus::EndOfFile && m_pendingAudio.empty()) {
            return DecodeStatus::EndOfFile;
        }
    }

    out = std::move(m_pendingAudio.front());
    m_pendingAudio.pop_front();
    return DecodeStatus::Ok;
}

// ---------------------------------------------------------------------------
// Seeking
// ---------------------------------------------------------------------------

bool MediaDecoder::seekToFrameIndex(int64_t index, QString* error)
{
    if (!m_open || !m_metadata.hasVideo) {
        return false;
    }

    const int64_t target = std::max<int64_t>(index, 0);
    const AVRational rate{ m_metadata.frameRate.numerator, m_metadata.frameRate.denominator };
    const AVRational timeBase{ m_metadata.videoTimeBase.numerator,
                               m_metadata.videoTimeBase.denominator };

    const int64_t targetPts =
        ffmpeg::frameIndexToPts(target, rate, timeBase, m_metadata.videoStartTime);

    // AVSEEK_FLAG_BACKWARD asks for the keyframe at or before the target, which
    // is what allows decoding forward to land on the exact frame. Seeking
    // forward would overshoot and make the requested frame unreachable.
    const int result = av_seek_frame(m_format.get(), m_metadata.videoStreamIndex,
                                     targetPts, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        if (error) {
            *error = QStringLiteral("Seek failed: %1").arg(ffmpeg::errorString(result));
        }
        qCWarning(log::media).noquote() << ffmpeg::errorString("av_seek_frame", result);
        return false;
    }

    // Buffered decoder state belongs to the old position and would otherwise be
    // presented as if it belonged to the new one.
    if (m_videoCodec) {
        avcodec_flush_buffers(m_videoCodec.get());
    }
    if (m_audioCodec) {
        avcodec_flush_buffers(m_audioCodec.get());
    }
    if (m_resampler) {
        // Drop samples the resampler is holding from before the seek.
        swr_init(m_resampler.get());
    }

    m_pendingVideo.clear();
    m_pendingAudio.clear();

    m_demuxEof = false;
    m_videoEof = false;
    m_audioEof = false;
    m_draining = false;
    m_nextVideoFrameIndex = target;
    m_nextAudioPtsUs = ffmpeg::frameIndexToMicroseconds(target, rate);

    qCDebug(log::media) << "Seek to frame" << target << "pts" << targetPts;
    return true;
}

bool MediaDecoder::frameAtIndex(int64_t index, VideoFrame& out, QString* error)
{
    if (!m_open || !m_metadata.hasVideo) {
        if (error) {
            *error = QStringLiteral("No media is open.");
        }
        return false;
    }

    const int64_t target = std::max<int64_t>(index, 0);

    // Fast path: the requested frame is simply the next one, so no seek is
    // needed. This is what makes forward stepping and normal playback cheap.
    if (target == m_nextVideoFrameIndex && !m_pendingVideo.empty()) {
        out = std::move(m_pendingVideo.front());
        m_pendingVideo.pop_front();
        if (out.frameIndex == target) {
            return true;
        }
        // Fell through: the queued frame was not the expected one, so seek.
    }

    int64_t seekTarget = target;

    for (int attempt = 0; attempt <= kSeekRetryAttempts; ++attempt) {
        if (!seekToFrameIndex(seekTarget, error)) {
            return false;
        }

        VideoFrame frame;
        int steps = 0;
        bool overshot = false;

        while (true) {
            const DecodeStatus status = nextVideoFrame(frame, error);
            if (status == DecodeStatus::Error) {
                return false;
            }
            if (status == DecodeStatus::EndOfFile) {
                // The file ended before reaching the target. If anything was
                // decoded, the last frame is the closest answer available.
                if (frame.isValid()) {
                    out = std::move(frame);
                    return true;
                }
                if (error) {
                    *error = QStringLiteral("Frame %1 is past the end of the media.").arg(target);
                }
                return false;
            }

            if (frame.frameIndex == target) {
                out = std::move(frame);
                return true;
            }
            if (frame.frameIndex > target) {
                // The seek landed after the requested frame. Retry from further
                // back rather than showing the wrong frame.
                overshot = true;
                break;
            }
            if (++steps > kMaxDecodeStepsPerSeek) {
                if (error) {
                    *error = QStringLiteral("Could not reach frame %1 within the step limit.")
                                 .arg(target);
                }
                return false;
            }
        }

        if (!overshot) {
            break;
        }

        // Step back roughly a second at a time looking for an earlier keyframe.
        const int64_t stepBack =
            std::max<int64_t>(1, static_cast<int64_t>(std::lround(m_metadata.frameRate.toDouble())));
        seekTarget -= stepBack * (attempt + 1);
        if (seekTarget <= 0) {
            seekTarget = 0;
            if (attempt > 0) {
                break;
            }
        }
    }

    if (error && error->isEmpty()) {
        *error = QStringLiteral("Could not decode frame %1.").arg(target);
    }
    return false;
}

int64_t MediaDecoder::countFramesExactly(QString* error)
{
    if (!m_open || !m_metadata.hasVideo) {
        return -1;
    }

    // Only worth doing for short media; see kMaxFramesToCount.
    const int64_t estimate = m_metadata.effectiveFrameCount();
    if (estimate > kMaxFramesToCount) {
        qCDebug(log::media) << "Skipping exact frame count: estimated" << estimate
                            << "frames exceeds the limit";
        return -1;
    }

    if (!seekToFrameIndex(0, error)) {
        return -1;
    }

    int64_t count = 0;
    int64_t highestIndex = -1;
    VideoFrame frame;

    while (count <= kMaxFramesToCount) {
        const DecodeStatus status = nextVideoFrame(frame, error);
        if (status == DecodeStatus::EndOfFile) {
            break;
        }
        if (status == DecodeStatus::Error) {
            return -1;
        }
        highestIndex = std::max(highestIndex, frame.frameIndex);
        ++count;
    }

    seekToFrameIndex(0, nullptr);

    if (count > kMaxFramesToCount) {
        return -1;
    }

    // Trust whichever is larger: a stream whose indices are sparse still has
    // that many distinct presentation positions.
    return std::max(count, highestIndex + 1);
}

} // namespace atk::media
