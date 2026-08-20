#pragma once

#include "media/AudioBuffer.h"
#include "media/MediaMetadata.h"
#include "media/VideoFrame.h"
#include "media/ffmpeg/FFmpegRaii.h"

#include <QString>

#include <cstdint>
#include <deque>
#include <functional>

namespace atk::media {

/// Outcome of a decode step.
enum class DecodeStatus {
    Ok,         ///< Produced something.
    EndOfFile,  ///< The stream is exhausted and fully drained.
    Error,      ///< Failed; see the error string.
};

/// The FFmpeg decoder.
///
/// Owns every FFmpeg object for one media file: the demuxer, both codec
/// contexts, the scaler and the resampler. All of them are held by the RAII
/// types in ffmpeg/FFmpegRaii.h, so closing a file or hitting an error path
/// cannot leak.
///
/// THREADING
/// ---------
/// This class is **not** thread-safe and is owned by exactly one thread: the
/// decode thread created by DecoderWorker. Nothing on the UI thread may touch a
/// MediaDecoder, because FFmpeg contexts cannot be driven from two threads at
/// once. The UI communicates with it only through DecoderWorker's queued
/// signals.
///
/// FRAME INDEXING
/// --------------
/// Every decoded frame carries both its stream timestamp and a frame index
/// derived from it by exact rational arithmetic (see ffmpeg/FFmpegUtil.h).
/// Indices are never obtained by counting decoded frames, because a counter
/// becomes wrong the moment anything seeks.
class MediaDecoder {
public:
    MediaDecoder();
    ~MediaDecoder();

    MediaDecoder(const MediaDecoder&) = delete;
    MediaDecoder& operator=(const MediaDecoder&) = delete;

    /// Opens and probes a file. On failure the decoder is left closed and
    /// `error` receives a user-presentable message.
    bool open(const QString& filePath, QString* error);

    /// Releases every FFmpeg resource. Safe to call when already closed.
    void close();

    bool isOpen() const { return m_open; }
    const MediaMetadata& metadata() const { return m_metadata; }

    // --- Video ------------------------------------------------------------

    /// Produces the next video frame in presentation order.
    DecodeStatus nextVideoFrame(VideoFrame& out, QString* error);

    /// Asked repeatedly during long decode loops; returning true abandons the
    /// work in progress.
    ///
    /// Seeking to a frame deep inside a long GOP can mean decoding hundreds of
    /// pictures. If the user has already moved on, finishing that decode is
    /// pure waste and delays the frame they actually want, so the loops check
    /// this and give up early.
    using CancelPredicate = std::function<bool()>;

    /// Decodes exactly frame `index`, seeking first when it is not simply the
    /// next frame.
    ///
    /// This is what frame stepping and timeline seeking both use: it seeks to
    /// the keyframe at or before the target and then decodes forward to the
    /// requested presentation frame, so the frame returned is the one asked
    /// for -- never the nearest keyframe.
    bool frameAtIndex(int64_t index, VideoFrame& out, QString* error,
                      const CancelPredicate& isCancelled = {});

    /// The index the next call to nextVideoFrame() is expected to produce.
    int64_t nextFrameIndex() const { return m_nextVideoFrameIndex; }

    /// True once the video stream has been fully drained.
    bool atEndOfVideo() const { return m_videoEof && m_pendingVideo.empty(); }

    // --- Audio ------------------------------------------------------------

    /// Configures resampling to the format the output device accepts. Must be
    /// called after open() and before any audio is decoded.
    bool configureAudioOutput(const AudioFormat& format, QString* error);

    /// The format audio is currently being resampled to.
    AudioFormat outputAudioFormat() const { return m_outputAudioFormat; }

    /// Produces the next chunk of resampled interleaved PCM.
    DecodeStatus nextAudioChunk(AudioChunk& out, QString* error);

    // --- Positioning ------------------------------------------------------

    /// Seeks so decoding resumes at or before `index`, flushing both codecs and
    /// discarding buffered output. Does not decode.
    bool seekToFrameIndex(int64_t index, QString* error);

    /// Counts the video frames by decoding the whole stream, then restores the
    /// previous position. Only used for short media where the container gave no
    /// trustworthy count -- see kMaxFramesToCount.
    ///
    /// Returns -1 when the count was abandoned as too expensive.
    int64_t countFramesExactly(QString* error);

    /// Upper bound on frames countFramesExactly() will decode before giving up.
    /// A full scan of a feature-length movie to fill in a status-bar number is
    /// not a trade worth making; an estimate is shown instead, marked as one.
    static constexpr int64_t kMaxFramesToCount = 20000;

private:
    /// Reads one packet and routes it to whichever decoder owns its stream,
    /// appending any output to the pending queues.
    DecodeStatus pump(QString* error);

    /// Drains frames already buffered inside the codecs at end of stream.
    DecodeStatus drain(QString* error);

    bool openVideoStream(QString* error);
    bool openAudioStream(QString* error);
    void readMetadata();

    /// Converts a decoded AVFrame to a display-ready VideoFrame.
    bool convertVideoFrame(const AVFrame* source, VideoFrame& out, QString* error);

    /// Converts and appends a decoded audio AVFrame to the pending queue.
    bool convertAudioFrame(const AVFrame* source, QString* error);

    /// Computes the frame index for a decoded video frame's timestamp.
    int64_t frameIndexForTimestamp(int64_t pts) const;

    void resetStreamState();

    /// True when `isCancelled` is set and reports cancellation.
    static bool cancelled(const CancelPredicate& isCancelled);

    // --- FFmpeg objects, all RAII-owned -----------------------------------
    ffmpeg::FormatContextPtr m_format;
    ffmpeg::CodecContextPtr m_videoCodec;
    ffmpeg::CodecContextPtr m_audioCodec;
    ffmpeg::SwsContextPtr m_scaler;
    ffmpeg::SwrContextPtr m_resampler;
    ffmpeg::PacketPtr m_packet;
    ffmpeg::FramePtr m_frame;

    MediaMetadata m_metadata;
    AudioFormat m_outputAudioFormat;

    /// Decoded output waiting to be taken. Both streams are decoded from one
    /// packet sequence, so whichever the caller is not asking for right now has
    /// to be held rather than dropped.
    std::deque<VideoFrame> m_pendingVideo;
    std::deque<AudioChunk> m_pendingAudio;

    int64_t m_nextVideoFrameIndex = 0;
    /// PTS of the next audio sample to be emitted, in output-format terms.
    int64_t m_nextAudioPtsUs = 0;

    bool m_open = false;
    bool m_demuxEof = false;   ///< av_read_frame returned EOF
    bool m_videoEof = false;   ///< video codec fully drained
    bool m_audioEof = false;   ///< audio codec fully drained
    bool m_draining = false;   ///< flush packets have been sent
};

} // namespace atk::media
