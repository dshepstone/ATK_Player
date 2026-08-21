#pragma once

#include "media/AudioBuffer.h"
#include "media/ffmpeg/FFmpegRaii.h"

#include <QString>

#include <cstdint>
#include <functional>

namespace atk::media {

/// Reads and resamples a file's audio stream, independently of video.
///
/// WHY THIS IS SEPARATE FROM MediaDecoder
/// --------------------------------------
/// Two reasons, both load-bearing.
///
/// First, MediaDecoder requires a video stream and drives one demux sequence
/// for both streams. Milestone M1 ended by making video scrubbing *keep* its
/// decoder position so nearby targets decode forward instead of re-seeking, and
/// that locality is what makes slow scrubbing usable. Pulling audio grains out
/// of the same decoder would re-seek it constantly and destroy exactly the
/// behaviour that was just fixed.
///
/// Second, waveform analysis is a linear scan of the whole file. Running it
/// through the playback decoder would drag the playhead's decoder from one end
/// of the media to the other while the user is trying to review a shot.
///
/// So audio review work gets its own file handle and its own contexts. FFmpeg
/// is perfectly happy to have the same file open twice, and the two paths then
/// cannot interfere.
///
/// THREADING
/// ---------
/// Not thread-safe, and owned by exactly one thread -- the same rule as
/// MediaDecoder. Each worker that needs one creates its own.
class AudioSourceReader {
public:
    AudioSourceReader();
    ~AudioSourceReader();

    AudioSourceReader(const AudioSourceReader&) = delete;
    AudioSourceReader& operator=(const AudioSourceReader&) = delete;

    /// Opens the file's best audio stream and configures resampling to
    /// `format`. Fails when the file has no audio ATK Player can decode.
    bool open(const QString& filePath, const AudioFormat& format, QString* error);

    void close();
    bool isOpen() const { return m_open; }

    /// The format audio is resampled to.
    AudioFormat format() const { return m_format; }

    /// Duration of the audio stream in microseconds, -1 when unknown.
    int64_t durationUs() const { return m_durationUs; }

    /// First presentation timestamp of the audio stream, in microseconds.
    ///
    /// Not always zero. A container can start its audio and video at different
    /// timestamps, and ignoring that offset would slide every waveform bucket
    /// and every scrub grain against the picture by a constant.
    int64_t startTimeUs() const { return m_startTimeUs; }

    /// Positions the reader so the next read returns samples at or before
    /// `mediaUs`, flushing the decoder. Timestamps are media time, matching
    /// what the timeline and the video decoder use.
    bool seekToMicroseconds(int64_t mediaUs, QString* error);

    /// Reads the next chunk of resampled interleaved PCM in stream order.
    /// Returns false at end of stream or on error.
    bool readChunk(AudioChunk& out, QString* error);

    /// Reads `durationUs` of PCM starting at `startUs`.
    ///
    /// Seeks, then discards up to the requested position so the returned buffer
    /// begins where it was asked to rather than at whatever the demuxer landed
    /// on. `actualStartUs` reports where it truly begins, which is what the
    /// scrub accuracy test asserts on.
    bool readRange(int64_t startUs, int64_t durationUs, QByteArray& pcm,
                   int64_t* actualStartUs, QString* error);

    /// Asked periodically during long scans; returning true abandons the work.
    using CancelPredicate = std::function<bool()>;

    /// Scans the whole stream from the beginning, handing each decoded chunk to
    /// `sink`. Used by waveform analysis. Stops early if `isCancelled` fires.
    bool scan(const std::function<void(const AudioChunk&)>& sink,
              const CancelPredicate& isCancelled, QString* error);

private:
    bool openStream(QString* error);
    bool configureResampler(QString* error);
    /// Pulls one packet and decodes it, appending PCM to `out` when produced.
    bool decodeNext(AudioChunk& out, bool& produced, bool& endOfStream, QString* error);

    ffmpeg::FormatContextPtr m_format_ctx;
    ffmpeg::CodecContextPtr m_codec;
    ffmpeg::SwrContextPtr m_resampler;
    ffmpeg::PacketPtr m_packet;
    ffmpeg::FramePtr m_frame;

    AudioFormat m_format;
    int m_streamIndex = -1;
    int64_t m_durationUs = -1;
    int64_t m_startTimeUs = 0;
    bool m_open = false;
    bool m_endOfStream = false;
};

} // namespace atk::media
