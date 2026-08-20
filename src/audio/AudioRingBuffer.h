#pragma once

#include <QByteArray>
#include <QMutex>

#include <cstdint>

namespace atk::audio {

/// A bounded, thread-safe PCM queue between the decode thread and the audio
/// device.
///
/// Bounded is the important word. The decoder can produce audio far faster than
/// real time, so an unbounded queue would swallow an entire film's audio into
/// RAM within seconds of pressing play. Writes stop at the cap and report how
/// much was actually taken, which is what throttles the decoder to roughly the
/// speed of playback.
///
/// It also tracks the media timestamp of the next byte to be read, so the audio
/// device can report a *media* position rather than an elapsed byte count.
/// That position is what video synchronises against.
class AudioRingBuffer {
public:
    /// Default capacity, in bytes. At 48 kHz stereo 16-bit (192 kB/s) this is
    /// about two seconds -- enough to ride out a decode hiccup, short enough
    /// that a pause or seek does not leave a long stale tail audible.
    static constexpr int64_t kDefaultCapacityBytes = 384 * 1024;

    explicit AudioRingBuffer(int64_t capacityBytes = kDefaultCapacityBytes);

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;

    /// Sets the byte rate used to advance the read timestamp as data is
    /// consumed. Must be called before writing; changing it clears the buffer,
    /// because timestamps already queued were computed at the old rate.
    void setBytesPerSecond(int bytesPerSecond);
    int bytesPerSecond() const;

    /// Appends as much of `data` as fits. Returns the number of bytes taken,
    /// which may be zero when full; the caller is expected to retry later
    /// rather than block.
    ///
    /// `ptsUs` is the media timestamp of the first byte of `data`. It is used
    /// only when the buffer was empty, since otherwise the timestamp is implied
    /// by what is already queued.
    int64_t write(const char* data, int64_t size, int64_t ptsUs);

    /// Copies up to `maxSize` bytes out. Returns the number of bytes written to
    /// `destination`. Short reads are normal and mean underrun.
    int64_t read(char* destination, int64_t maxSize);

    /// Discards everything. Used on seek, on stop and when media changes, so no
    /// audio from the previous position can be heard afterwards.
    void clear();

    int64_t bytesAvailable() const;
    int64_t freeSpace() const;
    int64_t capacity() const { return m_capacity; }
    bool isEmpty() const { return bytesAvailable() == 0; }

    /// Media timestamp of the next byte a reader will receive, in microseconds.
    /// -1 when nothing is buffered.
    int64_t nextReadPtsUs() const;

private:
    int64_t bytesAvailableLocked() const;

    mutable QMutex m_mutex;
    QByteArray m_buffer;
    int64_t m_capacity;
    /// Media timestamp of the first byte currently held in m_buffer.
    int64_t m_headPtsUs = -1;
    int m_bytesPerSecond = 0;
};

} // namespace atk::audio
