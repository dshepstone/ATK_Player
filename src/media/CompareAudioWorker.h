#pragma once
#include "media/AudioBuffer.h"
#include <QObject>
#include <memory>
namespace atk::audio { class AudioRingBuffer; }
namespace atk::media {
class AudioSourceReader;
class CompareAudioWorker final : public QObject {
    Q_OBJECT
public:
    CompareAudioWorker(std::shared_ptr<audio::AudioRingBuffer> buffer, QObject* parent = nullptr);
    ~CompareAudioWorker() override;
    static bool validateSource(const QString& path, QString* error);
public slots:
    void openSource(const QString& path, int sampleRate, int channelCount, quint64 generation);
    void closeSource(quint64 generation);
    void startAt(qint64 providerTimeUs, qint64 masterEpochUs, quint64 generation);
    void stop(quint64 generation);
    void shutdown();
signals:
    void sourceReady(qint64 durationUs, qint64 startTimeUs, quint64 generation);
    void sourceUnavailable(const QString& message, quint64 generation);
    void audioPrimed(int bufferedMs, qint64 masterEpochUs, quint64 generation);
private slots:
    void pump();
private:
    std::shared_ptr<audio::AudioRingBuffer> m_buffer;
    std::unique_ptr<AudioSourceReader> m_reader;
    AudioFormat m_format;
    quint64 m_generation = 0;
    qint64 m_cursorUs = 0;
    qint64 m_masterEpochUs = 0;
    bool m_running = false;
    bool m_primed = false;
    bool m_shuttingDown = false;
};
}
