#pragma once

#include "media/decoders/IDecoder.h"

namespace atk::media {

/// FFmpeg-backed decoder.
///
/// PHASE 0 STATUS: declaration only. Every method fails cleanly and records an
/// error; no FFmpeg headers are included and no FFmpeg libraries are linked, so
/// the application builds and runs without FFmpeg present.
///
/// Implementation plan (milestone M1):
///   1. Link libavformat / libavcodec / libavutil / libswscale dynamically.
///      LGPL builds only -- GPL and nonfree components stay disabled, see
///      docs/THIRD_PARTY_LICENSES.md.
///   2. open():   avformat_open_input, avformat_find_stream_info, pick the best
///                video stream, open the codec, populate MediaMetadata.
///   3. seek():   av_seek_frame to the keyframe at or before the target, then
///                decode forward to the exact frame.
///   4. decode(): av_read_frame / avcodec_send_packet / avcodec_receive_frame,
///                then sws_scale into the QImage inside VideoFrame.
///   5. Run on a dedicated decode thread feeding FrameCache; the UI thread
///      never blocks on a decode.
///   6. close():  release every context, in reverse order of acquisition.
class FFmpegDecoder final : public IDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder() override;

    bool open(const QString& filePath) override;
    void close() override;
    bool isOpen() const override;

    const MediaMetadata& metadata() const override;
    bool decodeFrame(int64_t frameNumber, VideoFrame& out) override;
    QString lastError() const override;

private:
    MediaMetadata m_metadata;
    QString m_lastError;
    bool m_open = false;
};

} // namespace atk::media
