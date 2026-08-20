#include "audio/AudioOutput.h"

#include "audio/AudioRingBuffer.h"
#include "core/Logging.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cstring>
#include <utility>

namespace atk::audio {
namespace {

/// Bridges the ring buffer to QAudioSink's pull model.
///
/// On underrun it emits silence and returns a full read rather than zero. A
/// zero-length read puts QAudioSink into IdleState and it stops pulling, which
/// would stall playback permanently the first time decoding fell behind.
/// Silence keeps the device running until real data arrives again.
///
/// Only real bytes are counted towards the media clock, so inserted silence
/// cannot make the clock run ahead of the audio actually heard.
class RingBufferDevice : public QIODevice {
public:
    explicit RingBufferDevice(std::shared_ptr<AudioRingBuffer> buffer, QObject* parent = nullptr)
        : QIODevice(parent)
        , m_buffer(std::move(buffer))
    {
    }

    void resetCounters() { m_consumedBytes = 0; m_silenceBytes = 0; }
    int64_t consumedBytes() const { return m_consumedBytes; }
    int64_t silenceBytes() const { return m_silenceBytes; }

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (data == nullptr || maxSize <= 0) {
            return 0;
        }

        const int64_t got = m_buffer ? m_buffer->read(data, maxSize) : 0;
        m_consumedBytes += got;

        if (got < maxSize) {
            std::memset(data + got, 0, static_cast<std::size_t>(maxSize - got));
            m_silenceBytes += (maxSize - got);
            return maxSize;
        }
        return got;
    }

    qint64 writeData(const char*, qint64) override { return 0; }

private:
    std::shared_ptr<AudioRingBuffer> m_buffer;
    int64_t m_consumedBytes = 0;
    int64_t m_silenceBytes = 0;
};

} // namespace

AudioOutput::AudioOutput(std::shared_ptr<AudioRingBuffer> buffer, QObject* parent)
    : QObject(parent)
    , m_buffer(std::move(buffer))
{
}

AudioOutput::~AudioOutput()
{
    stop();
}

bool AudioOutput::open(int preferredSampleRate, int preferredChannelCount)
{
    stop();

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        // Headless CI and machines with no sound card land here. Video-only
        // playback still has to work, so this is a normal outcome.
        qCInfo(log::playback) << "No audio output device available; continuing without audio";
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(preferredSampleRate > 0 ? preferredSampleRate : 48000);
    format.setChannelCount(preferredChannelCount > 0 ? preferredChannelCount : 2);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!device.isFormatSupported(format)) {
        // Fall back to whatever the device prefers rather than refusing to play.
        const QAudioFormat preferred = device.preferredFormat();
        qCInfo(log::playback).noquote()
            << "Requested audio format unsupported; using device preferred"
            << preferred.sampleRate() << "Hz" << preferred.channelCount() << "ch";

        format.setSampleRate(preferred.sampleRate());
        format.setChannelCount(preferred.channelCount());
        format.setSampleFormat(QAudioFormat::Int16);

        if (!device.isFormatSupported(format)) {
            qCWarning(log::playback) << "Audio device supports no usable 16-bit format";
            return false;
        }
    }

    m_sink = std::make_unique<QAudioSink>(device, format);

    m_format.sampleRate = format.sampleRate();
    m_format.channelCount = format.channelCount();
    m_format.bytesPerSample = 2;

    if (m_buffer) {
        m_buffer->setBytesPerSecond(m_format.bytesPerSecond());
    }

    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::StoppedState && m_sink && m_sink->error() != QAudio::NoError) {
            const QString message =
                QStringLiteral("Audio device error (%1)").arg(static_cast<int>(m_sink->error()));
            qCWarning(log::playback).noquote() << message;
            emit deviceError(message);
        }
    });

    qCInfo(log::playback).noquote()
        << "Audio output opened:" << device.description()
        << m_format.sampleRate << "Hz" << m_format.channelCount << "ch s16";

    applyVolume();
    return true;
}

void AudioOutput::start(int64_t startPtsUs)
{
    if (!m_sink) {
        return;
    }

    // A fresh start() rather than resume(): it discards whatever the device
    // still had queued, which is exactly what must happen after a seek so no
    // audio from the old position is heard.
    m_sink->stop();

    auto* device = new RingBufferDevice(m_buffer, this);
    device->open(QIODevice::ReadOnly);

    delete m_device;
    m_device = device;

    m_startPtsUs = startPtsUs;
    m_sink->start(m_device);
    m_running = true;
    applyVolume();
}

void AudioOutput::stop()
{
    if (m_sink) {
        m_sink->stop();
    }
    if (m_device != nullptr) {
        m_device->close();
        delete m_device;
        m_device = nullptr;
    }
    if (m_buffer) {
        m_buffer->clear();
    }
    m_running = false;
    m_startPtsUs = -1;
}

void AudioOutput::pause()
{
    if (m_sink && m_running) {
        m_sink->suspend();
        m_running = false;
    }
}

void AudioOutput::resume()
{
    if (m_sink && !m_running && m_device != nullptr) {
        m_sink->resume();
        m_running = true;
    }
}

int64_t AudioOutput::positionUs() const
{
    if (!m_sink || m_startPtsUs < 0 || !m_format.isValid()) {
        return -1;
    }

    const auto* device = qobject_cast<const RingBufferDevice*>(m_device);
    if (device == nullptr) {
        return -1;
    }

    // Media time consumed from the buffer, minus what the device is still
    // holding but has not played. Silence inserted on underrun is excluded, so
    // the clock never runs ahead of the audio actually heard.
    const int64_t consumedUs = m_format.bytesToMicroseconds(device->consumedBytes());
    const int64_t queuedBytes =
        std::max<int64_t>(0, m_sink->bufferSize() - m_sink->bytesFree());
    const int64_t queuedUs = m_format.bytesToMicroseconds(queuedBytes);

    return std::max<int64_t>(m_startPtsUs, m_startPtsUs + consumedUs - queuedUs);
}

void AudioOutput::setVolume(qreal volume)
{
    m_volume = std::clamp<qreal>(volume, 0.0, 1.0);
    applyVolume();
}

void AudioOutput::setMuted(bool muted)
{
    m_muted = muted;
    applyVolume();
}

void AudioOutput::applyVolume()
{
    if (m_sink) {
        m_sink->setVolume(m_muted ? 0.0 : m_volume);
    }
}

} // namespace atk::audio
