#pragma once

#include "media/AudioBuffer.h"

#include <QObject>

#include <memory>

class QAudioSink;
class QIODevice;

namespace atk::audio {

class AudioRingBuffer;

/// Plays the decoded PCM stream through the system audio device, and reports
/// where in the media that audio currently is.
///
/// The position report is the important part. When a file has audio, audio is
/// the synchronisation master: it cannot be stretched or skipped without being
/// audible, whereas a video frame shown a moment late is not. Video is
/// therefore displayed according to what the audio device has actually
/// consumed, which is what positionUs() returns.
///
/// Lives on the UI thread and pulls from a ring buffer the decode thread fills.
class AudioOutput : public QObject {
    Q_OBJECT

public:
    explicit AudioOutput(std::shared_ptr<AudioRingBuffer> buffer, QObject* parent = nullptr);
    ~AudioOutput() override;

    /// Opens the default output device.
    ///
    /// The requested format is adjusted to what the device actually supports;
    /// query actualFormat() afterwards, because the decoder must resample to
    /// that, not to what was asked for. Returns false when no device is
    /// available -- which is normal in CI and must not be fatal.
    bool open(int preferredSampleRate, int preferredChannelCount);

    /// True when a device was opened successfully.
    bool isOpen() const { return m_sink != nullptr; }

    /// The format the device accepted, and therefore the resampler's target.
    media::AudioFormat actualFormat() const { return m_format; }

    /// Begins pulling from the ring buffer. `startPtsUs` is the media timestamp
    /// the first buffered sample corresponds to, and anchors the audio clock.
    void start(int64_t startPtsUs);

    /// Stops output and discards anything queued in the device.
    void stop();

    /// Suspends without discarding, so resume() continues where it left off.
    void pause();
    void resume();

    bool isRunning() const { return m_running; }

    /// Media position of the audio currently being heard, in microseconds.
    /// Returns -1 when no meaningful position exists yet.
    int64_t positionUs() const;

    // --- Diagnostics ------------------------------------------------------
    //
    // "The device opened" says nothing about whether sound is coming out. These
    // separate real decoded audio from the silence inserted on underrun, which
    // is the difference between working audio and a device dutifully playing
    // nothing.

    /// Milliseconds of real PCM currently queued for playback.
    int64_t bufferedMs() const;

    /// Times the device asked for audio and the buffer had none.
    int64_t underrunCount() const;

    /// Bytes of real decoded audio the device has taken.
    int64_t realBytesConsumed() const;

    /// Bytes of silence inserted because nothing was queued. After startup this
    /// should stay at zero during healthy playback.
    int64_t silenceBytesInserted() const;

    /// True once the device has actually consumed real audio, as opposed to
    /// having merely been opened and started.
    bool isDeliveringAudio() const { return realBytesConsumed() > 0; }

    void setVolume(qreal volume);
    qreal volume() const { return m_volume; }

    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

signals:
    /// The device failed after having opened successfully.
    void deviceError(const QString& message);

private:
    void applyVolume();

    std::shared_ptr<AudioRingBuffer> m_buffer;
    std::unique_ptr<QAudioSink> m_sink;
    /// Owned by the sink once start() hands it over.
    QIODevice* m_device = nullptr;

    media::AudioFormat m_format;
    int64_t m_startPtsUs = -1;
    bool m_running = false;
    bool m_muted = false;
    qreal m_volume = 1.0;
};

} // namespace atk::audio
