#include "export/FFmpegExporter.h"
#include "media/ffmpeg/FFmpegUtil.h"
#include <QFile>
#include <QUuid>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace atk::exporter {
using namespace media::ffmpeg;

FFmpegExporter::FFmpegExporter() = default;
FFmpegExporter::~FFmpegExporter() { reset(); }

QString FFmpegExporter::availableH264Encoder()
{
    for (const char* name : {"h264_mf", "h264_d3d12va"})
        if (avcodec_find_encoder_by_name(name)) return QString::fromLatin1(name);
    return {};
}
bool FFmpegExporter::aacAvailable() { return avcodec_find_encoder_by_name("aac") != nullptr; }
bool FFmpegExporter::isSupported() { return !availableH264Encoder().isEmpty(); }

bool FFmpegExporter::open(const ExportSpec& spec, bool includeAudio, QString* error)
{
    reset(); m_spec = spec;
    m_temporaryPath = spec.outputPath + QStringLiteral(".atkpart.")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".mp4");
    AVFormatContext* raw = nullptr;
    int rc = avformat_alloc_output_context2(&raw, nullptr, "mp4", m_temporaryPath.toUtf8().constData());
    if (rc < 0 || !raw) { if (error) *error = QStringLiteral("Unable to create MP4 output."); return false; }
    m_format.reset(raw);
    const QByteArray videoName = spec.videoEncoder.toLatin1();
    const AVCodec* video = avcodec_find_encoder_by_name(videoName.constData());
    if (!video || video->id != AV_CODEC_ID_H264) { if (error) *error = QStringLiteral("No compatible H.264 encoder is available."); return false; }
    m_videoStream = avformat_new_stream(m_format.get(), nullptr);
    m_videoCodec.reset(avcodec_alloc_context3(video));
    if (!m_videoStream || !m_videoCodec) { if (error) *error = QStringLiteral("Unable to allocate the video encoder."); return false; }
    const QSize size = spec.outputSize();
    m_videoCodec->width = size.width(); m_videoCodec->height = size.height();
    m_videoCodec->pix_fmt = AV_PIX_FMT_YUV420P;
    m_videoCodec->time_base = {spec.sourceA.metadata.videoTimeBase.numerator,
                               spec.sourceA.metadata.videoTimeBase.denominator};
    m_videoCodec->framerate = {spec.sourceA.metadata.frameRate.numerator,
                               spec.sourceA.metadata.frameRate.denominator};
    m_videoCodec->bit_rate = 8'000'000;
    m_videoCodec->gop_size = std::max(1, static_cast<int>(spec.sourceA.metadata.frameRate.toDouble() * 2));
    if (m_format->oformat->flags & AVFMT_GLOBALHEADER) m_videoCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(m_videoCodec->priv_data, "rate_control", "quality", 0);
    av_opt_set_int(m_videoCodec->priv_data, "quality", 75, 0);
    rc = avcodec_open2(m_videoCodec.get(), video, nullptr);
    if (rc < 0) { if (error) *error = QStringLiteral("Unable to open H.264 encoder: %1").arg(errorString(rc)); return false; }
    m_videoStream->time_base = m_videoCodec->time_base;
    avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodec.get());
    if (includeAudio && aacAvailable()) {
        const AVCodec* audio = avcodec_find_encoder_by_name("aac");
        m_audioStream = avformat_new_stream(m_format.get(), nullptr);
        m_audioCodec.reset(avcodec_alloc_context3(audio));
        if (!m_audioStream || !m_audioCodec) { if (error) *error = QStringLiteral("Unable to allocate the AAC encoder."); return false; }
        m_audioCodec->sample_rate = spec.audioSampleRate; m_audioCodec->sample_fmt = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&m_audioCodec->ch_layout, spec.audioChannels);
        m_audioCodec->time_base = {1, spec.audioSampleRate}; m_audioCodec->bit_rate = 192'000;
        if (m_format->oformat->flags & AVFMT_GLOBALHEADER) m_audioCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        rc = avcodec_open2(m_audioCodec.get(), audio, nullptr);
        if (rc < 0) { if (error) *error = QStringLiteral("Unable to open AAC encoder: %1").arg(errorString(rc)); return false; }
        m_audioStream->time_base = m_audioCodec->time_base;
        avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodec.get());
    }
    rc = avio_open(&m_format->pb, m_temporaryPath.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (rc < 0) { if (error) *error = QStringLiteral("Unable to create the temporary output file."); return false; }
    rc = avformat_write_header(m_format.get(), nullptr);
    if (rc < 0) { if (error) *error = QStringLiteral("Unable to write the MP4 header: %1").arg(errorString(rc)); return false; }
    return true;
}

bool FFmpegExporter::writePacket(AVPacket* packet, AVCodecContext* codec, AVStream* stream, QString* error)
{
    av_packet_rescale_ts(packet, codec->time_base, stream->time_base); packet->stream_index = stream->index;
    const int rc = av_interleaved_write_frame(m_format.get(), packet);
    if (rc < 0 && error) *error = QStringLiteral("Unable to mux MP4 packet: %1").arg(errorString(rc));
    return rc >= 0;
}

bool FFmpegExporter::drainVideo(AVFrame* frame, QString* error)
{
    int rc = avcodec_send_frame(m_videoCodec.get(), frame);
    if (rc < 0) { if (error) *error = errorString("avcodec_send_frame(video)", rc); return false; }
    auto packet = makePacket();
    while ((rc = avcodec_receive_packet(m_videoCodec.get(), packet.get())) >= 0) {
        if (!writePacket(packet.get(), m_videoCodec.get(), m_videoStream, error)) return false;
        av_packet_unref(packet.get());
    }
    return rc == AVERROR(EAGAIN) || rc == AVERROR_EOF;
}

bool FFmpegExporter::encodeVideo(const QImage& source, qint64 pts, QString* error)
{
    QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    auto frame = makeFrame(); frame->format = m_videoCodec->pix_fmt; frame->width = m_videoCodec->width;
    frame->height = m_videoCodec->height; frame->pts = pts;
    if (av_frame_get_buffer(frame.get(), 32) < 0) return false;
    m_scaler.reset(sws_getCachedContext(m_scaler.release(), image.width(), image.height(), AV_PIX_FMT_RGBA,
        frame->width, frame->height, m_videoCodec->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr));
    const uint8_t* src[] = {image.constBits(), nullptr, nullptr, nullptr};
    const int strides[] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    if (!m_scaler || sws_scale(m_scaler.get(), src, strides, 0, image.height(), frame->data, frame->linesize) <= 0) {
        if (error) *error = QStringLiteral("Unable to convert an export frame to yuv420p."); return false;
    }
    return drainVideo(frame.get(), error);
}

bool FFmpegExporter::drainAudio(AVFrame* frame, QString* error)
{
    int rc = avcodec_send_frame(m_audioCodec.get(), frame);
    if (rc < 0) { if (error) *error = errorString("avcodec_send_frame(audio)", rc); return false; }
    auto packet = makePacket();
    while ((rc = avcodec_receive_packet(m_audioCodec.get(), packet.get())) >= 0) {
        if (!writePacket(packet.get(), m_audioCodec.get(), m_audioStream, error)) return false;
        av_packet_unref(packet.get());
    }
    return rc == AVERROR(EAGAIN) || rc == AVERROR_EOF;
}

bool FFmpegExporter::encodeAudio(const QByteArray& pcm, QString* error)
{
    if (!m_audioCodec || pcm.isEmpty()) return true;
    const auto* samples = reinterpret_cast<const int16_t*>(pcm.constData());
    const qint64 total = pcm.size() / (2 * m_spec.audioChannels);
    const int frameSize = m_audioCodec->frame_size > 0 ? m_audioCodec->frame_size : 1024;
    for (qint64 offset = 0; offset < total; offset += frameSize) {
        const int count = static_cast<int>(std::min<qint64>(frameSize, total - offset));
        auto frame = makeFrame(); frame->nb_samples = count; frame->format = AV_SAMPLE_FMT_FLTP;
        frame->sample_rate = m_spec.audioSampleRate; frame->pts = m_audioPts;
        av_channel_layout_copy(&frame->ch_layout, &m_audioCodec->ch_layout);
        if (av_frame_get_buffer(frame.get(), 0) < 0) return false;
        for (int c = 0; c < m_spec.audioChannels; ++c) {
            auto* dst = reinterpret_cast<float*>(frame->data[c]);
            for (int i = 0; i < count; ++i) dst[i] = samples[(offset + i) * m_spec.audioChannels + c] / 32768.0f;
        }
        if (!drainAudio(frame.get(), error)) return false;
        m_audioPts += count;
    }
    return true;
}

bool FFmpegExporter::finish(QString* error)
{
    if (m_finished) return true;
    if (!drainVideo(nullptr, error)) return false;
    if (m_audioCodec && !drainAudio(nullptr, error)) return false;
    const int rc = av_write_trailer(m_format.get());
    if (rc < 0) { if (error) *error = QStringLiteral("Unable to finalize MP4: %1").arg(errorString(rc)); return false; }
    if (m_format->pb) avio_closep(&m_format->pb);
    m_finished = true; return true;
}

void FFmpegExporter::discard()
{
    const QString path = m_temporaryPath;
    reset();
    if (!path.isEmpty()) QFile::remove(path);
}

void FFmpegExporter::reset()
{
    m_scaler.reset(); m_audioCodec.reset(); m_videoCodec.reset(); m_format.reset();
    m_videoStream = nullptr; m_audioStream = nullptr; m_audioPts = 0; m_finished = false;
}

} // namespace atk::exporter
