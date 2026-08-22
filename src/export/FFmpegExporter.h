#pragma once

#include "export/ExportSpec.h"
#include "media/ffmpeg/FFmpegRaii.h"
#include <QByteArray>
#include <QImage>

struct AVStream;

namespace atk::exporter {

class FFmpegExporter final {
public:
    FFmpegExporter();
    ~FFmpegExporter();
    FFmpegExporter(const FFmpegExporter&) = delete;
    FFmpegExporter& operator=(const FFmpegExporter&) = delete;
    static QString availableH264Encoder();
    static bool aacAvailable();
    static bool isSupported();
    bool open(const ExportSpec& spec, bool includeAudio, QString* error);
    bool encodeVideo(const QImage& image, qint64 outputPtsTicks, QString* error);
    bool encodeAudio(const QByteArray& interleavedS16, QString* error);
    bool finish(QString* error);
    /// Close all encoder/muxer handles and remove the incomplete sibling file.
    void discard();
    QString temporaryPath() const { return m_temporaryPath; }
private:
    bool drainVideo(AVFrame* frame, QString* error);
    bool drainAudio(AVFrame* frame, QString* error);
    bool writePacket(AVPacket* packet, AVCodecContext* codec, AVStream* stream, QString* error);
    void reset();
    ExportSpec m_spec;
    media::ffmpeg::OutputFormatContextPtr m_format;
    media::ffmpeg::CodecContextPtr m_videoCodec;
    media::ffmpeg::CodecContextPtr m_audioCodec;
    media::ffmpeg::SwsContextPtr m_scaler;
    AVStream* m_videoStream = nullptr;
    AVStream* m_audioStream = nullptr;
    QString m_temporaryPath;
    qint64 m_audioPts = 0;
    bool m_finished = false;
};

} // namespace atk::exporter
