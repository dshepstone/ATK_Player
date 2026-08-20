#include "audio/ScrubAudioEngine.h"

#include "core/Logging.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace atk::audio {
namespace {

/// Holds at most one pending grain and feeds it to the sink.
///
/// Silence is returned when nothing is queued, rather than a short read. A
/// zero-length read puts QAudioSink into IdleState and it stops pulling, and
/// then the next grain would not be heard until something restarted the device
/// -- which is the whole cost this class exists to avoid.
class GrainDevice : public QIODevice {
public:
    explicit GrainDevice(QObject* parent = nullptr)
        : QIODevice(parent)
    {
    }

    /// Replaces the pending grain. Returns true if one was discarded, which is
    /// latest-position-wins working as intended.
    bool submit(const QByteArray& pcm)
    {
        QMutexLocker locker(&m_mutex);
        const bool replaced = m_position < m_grain.size();
        m_grain = pcm;
        m_position = 0;
        return replaced;
    }

    void clear()
    {
        QMutexLocker locker(&m_mutex);
        m_grain.clear();
        m_position = 0;
    }

    bool isSequential() const override { return true; }

    /// Always claim data is available. The sink asks before reading, and an
    /// honest "nothing queued" would stop it pulling -- see the class comment.
    qint64 bytesAvailable() const override
    {
        return 4096 + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (data == nullptr || maxSize <= 0) {
            return 0;
        }

        QMutexLocker locker(&m_mutex);

        qint64 written = 0;
        const qint64 remaining = m_grain.size() - m_position;
        if (remaining > 0) {
            written = std::min(remaining, maxSize);
            std::memcpy(data, m_grain.constData() + m_position,
                        static_cast<std::size_t>(written));
            m_position += written;
        }

        if (written < maxSize) {
            std::memset(data + written, 0, static_cast<std::size_t>(maxSize - written));
        }
        return maxSize;
    }

    qint64 writeData(const char*, qint64) override { return 0; }

private:
    mutable QMutex m_mutex;
    QByteArray m_grain;
    qint64 m_position = 0;
};

} // namespace

ScrubAudioEngine::ScrubAudioEngine(QObject* parent)
    : QObject(parent)
{
}

ScrubAudioEngine::~ScrubAudioEngine()
{
    close();
}

bool ScrubAudioEngine::open(const media::AudioFormat& format)
{
    close();

    if (!format.isValid()) {
        return false;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        qCInfo(log::playback) << "No audio device for scrubbing; visual scrub only";
        return false;
    }

    QAudioFormat deviceFormat;
    deviceFormat.setSampleRate(format.sampleRate);
    deviceFormat.setChannelCount(format.channelCount);
    deviceFormat.setSampleFormat(QAudioFormat::Int16);

    if (!device.isFormatSupported(deviceFormat)) {
        qCWarning(log::playback) << "Scrub audio format unsupported by the device";
        return false;
    }

    m_sink = std::make_unique<QAudioSink>(device, deviceFormat);
    m_format = format;

    auto* grainDevice = new GrainDevice(this);
    grainDevice->open(QIODevice::ReadOnly);
    m_device = grainDevice;

    // Started once and left running for the whole session. Restarting per grain
    // would add the device's start latency to every mouse move.
    m_sink->start(m_device);
    applyVolume();

    qCInfo(log::playback).noquote()
        << "Scrub audio opened:" << device.description()
        << format.sampleRate << "Hz" << format.channelCount << "ch";
    return true;
}

void ScrubAudioEngine::close()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    if (m_device != nullptr) {
        m_device->close();
        delete m_device;
        m_device = nullptr;
    }
}

void ScrubAudioEngine::submitGrain(const QByteArray& pcm)
{
    if (!m_sink || m_device == nullptr || pcm.isEmpty()) {
        return;
    }

    ++m_submittedGrains;
    auto* grainDevice = static_cast<GrainDevice*>(m_device);
    if (grainDevice->submit(withFades(pcm))) {
        ++m_replacedGrains;
    }
}

void ScrubAudioEngine::flush()
{
    if (m_device != nullptr) {
        static_cast<GrainDevice*>(m_device)->clear();
    }
}

QByteArray ScrubAudioEngine::withFades(const QByteArray& pcm) const
{
    QByteArray shaped = pcm;

    const int frameBytes = m_format.bytesPerFrame();
    if (frameBytes <= 0 || shaped.size() < frameBytes * 4) {
        return shaped;
    }

    const int64_t totalFrames = shaped.size() / frameBytes;
    int64_t fadeFrames = (m_format.sampleRate * kFadeUs) / 1'000'000;

    // Never fade more than a quarter of the grain from each end, or a very
    // short grain would be almost entirely ramp and lose its transient.
    fadeFrames = std::clamp<int64_t>(fadeFrames, 1, totalFrames / 4);

    auto* samples = reinterpret_cast<int16_t*>(shaped.data());
    const int channels = m_format.channelCount;

    for (int64_t frame = 0; frame < fadeFrames; ++frame) {
        const float gain = static_cast<float>(frame) / static_cast<float>(fadeFrames);
        for (int channel = 0; channel < channels; ++channel) {
            auto& sample = samples[frame * channels + channel];
            sample = static_cast<int16_t>(std::lround(sample * gain));
        }
    }

    for (int64_t frame = 0; frame < fadeFrames; ++frame) {
        const float gain = static_cast<float>(frame) / static_cast<float>(fadeFrames);
        const int64_t position = totalFrames - 1 - frame;
        for (int channel = 0; channel < channels; ++channel) {
            auto& sample = samples[position * channels + channel];
            sample = static_cast<int16_t>(std::lround(sample * gain));
        }
    }

    return shaped;
}

void ScrubAudioEngine::setVolume(qreal volume)
{
    m_volume = std::clamp<qreal>(volume, 0.0, 1.0);
    applyVolume();
}

void ScrubAudioEngine::setMuted(bool muted)
{
    m_muted = muted;
    applyVolume();
    if (muted) {
        flush();
    }
}

void ScrubAudioEngine::applyVolume()
{
    if (m_sink) {
        m_sink->setVolume(m_muted ? 0.0 : m_volume);
    }
}

} // namespace atk::audio
