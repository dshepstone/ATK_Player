#pragma once

#include <QByteArray>

#include <cstdint>

namespace atk::media {

/// The PCM format ATK Player converts all audio to before it reaches the
/// output device.
///
/// Sources arrive in every imaginable layout -- planar float 5.1, mono 22 kHz,
/// whatever the codec produced. Rather than teaching the audio output about all
/// of them, libswresample converts everything to one interleaved format here.
/// The only values that vary are the ones the output device actually dictates.
struct AudioFormat {
    int sampleRate = 0;
    int channelCount = 0;
    /// Bytes per sample per channel. 2 for signed 16-bit.
    int bytesPerSample = 2;

    bool isValid() const
    {
        return sampleRate > 0 && channelCount > 0 && bytesPerSample > 0;
    }

    /// Bytes occupied by one sample across all channels.
    int bytesPerFrame() const { return channelCount * bytesPerSample; }

    /// Bytes of audio representing one second of playback.
    int bytesPerSecond() const { return sampleRate * bytesPerFrame(); }

    /// Duration in microseconds of `bytes` worth of this format.
    int64_t bytesToMicroseconds(int64_t bytes) const
    {
        const int64_t perSecond = bytesPerSecond();
        return perSecond > 0 ? (bytes * 1'000'000) / perSecond : 0;
    }

    /// Bytes needed to hold `microseconds` of this format.
    int64_t microsecondsToBytes(int64_t microseconds) const
    {
        const int64_t perSecond = bytesPerSecond();
        // Round down to a whole sample frame; a partial frame would desync the
        // channel interleaving for everything after it.
        const int64_t raw = (microseconds * perSecond) / 1'000'000;
        const int frameSize = bytesPerFrame();
        return frameSize > 0 ? (raw / frameSize) * frameSize : 0;
    }

    friend bool operator==(const AudioFormat&, const AudioFormat&) = default;
};

/// A chunk of decoded, resampled, interleaved PCM with the media timestamp of
/// its first sample.
///
/// The timestamp is what lets the audio output report a media position rather
/// than just an elapsed byte count, which is what makes audio usable as the
/// synchronisation master.
struct AudioChunk {
    /// Presentation timestamp of the first sample, microseconds from the start
    /// of the media. -1 when unset.
    int64_t ptsUs = -1;
    QByteArray pcm;

    bool isValid() const { return ptsUs >= 0 && !pcm.isEmpty(); }
};

} // namespace atk::media
