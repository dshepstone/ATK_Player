#include "audio/AudioRingBuffer.h"

#include <QMutexLocker>

#include <algorithm>
#include <cstring>

namespace atk::audio {

AudioRingBuffer::AudioRingBuffer(int64_t capacityBytes)
    : m_capacity(capacityBytes > 0 ? capacityBytes : kDefaultCapacityBytes)
{
    m_buffer.reserve(static_cast<qsizetype>(m_capacity));
}

void AudioRingBuffer::setBytesPerSecond(int bytesPerSecond)
{
    QMutexLocker locker(&m_mutex);
    if (m_bytesPerSecond == bytesPerSecond) {
        return;
    }
    m_bytesPerSecond = bytesPerSecond;
    // Anything already queued was timestamped against the old rate.
    m_buffer.clear();
    m_headPtsUs = -1;
}

int AudioRingBuffer::bytesPerSecond() const
{
    QMutexLocker locker(&m_mutex);
    return m_bytesPerSecond;
}

int64_t AudioRingBuffer::write(const char* data, int64_t size, int64_t ptsUs)
{
    if (data == nullptr || size <= 0) {
        return 0;
    }

    QMutexLocker locker(&m_mutex);

    const int64_t space = m_capacity - m_buffer.size();
    if (space <= 0) {
        return 0;
    }

    const int64_t take = std::min(space, size);

    if (m_buffer.isEmpty()) {
        // First bytes after a clear or a drain define the timeline position.
        m_headPtsUs = ptsUs;
    }

    m_buffer.append(data, static_cast<qsizetype>(take));
    return take;
}

int64_t AudioRingBuffer::read(char* destination, int64_t maxSize)
{
    if (destination == nullptr || maxSize <= 0) {
        return 0;
    }

    QMutexLocker locker(&m_mutex);

    const int64_t available = m_buffer.size();
    if (available <= 0) {
        return 0;
    }

    const int64_t take = std::min(available, maxSize);
    std::memcpy(destination, m_buffer.constData(), static_cast<std::size_t>(take));
    m_buffer.remove(0, static_cast<qsizetype>(take));

    // Advance the head timestamp by the duration of what was consumed, so the
    // next reader sees the media time of the audio it is about to receive.
    if (m_headPtsUs >= 0 && m_bytesPerSecond > 0) {
        m_headPtsUs += (take * 1'000'000) / m_bytesPerSecond;
    }
    if (m_buffer.isEmpty()) {
        m_headPtsUs = -1;
    }

    return take;
}

void AudioRingBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_buffer.clear();
    m_headPtsUs = -1;
}

int64_t AudioRingBuffer::bytesAvailableLocked() const
{
    return m_buffer.size();
}

int64_t AudioRingBuffer::bytesAvailable() const
{
    QMutexLocker locker(&m_mutex);
    return bytesAvailableLocked();
}

int64_t AudioRingBuffer::freeSpace() const
{
    QMutexLocker locker(&m_mutex);
    return std::max<int64_t>(0, m_capacity - m_buffer.size());
}

int64_t AudioRingBuffer::nextReadPtsUs() const
{
    QMutexLocker locker(&m_mutex);
    return m_headPtsUs;
}

} // namespace atk::audio
