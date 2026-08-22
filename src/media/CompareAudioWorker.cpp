#include "media/CompareAudioWorker.h"
#include "audio/AudioRingBuffer.h"
#include "media/AudioSourceReader.h"
#include <QTimer>
#include <algorithm>
namespace atk::media {
namespace { constexpr qint64 kChunkUs = 100'000; constexpr qint64 kPrimeMs = 200; constexpr qint64 kTargetMs = 400; }
CompareAudioWorker::CompareAudioWorker(std::shared_ptr<audio::AudioRingBuffer> buffer, QObject* parent)
    : QObject(parent), m_buffer(std::move(buffer)), m_reader(std::make_unique<AudioSourceReader>()) {}
CompareAudioWorker::~CompareAudioWorker() = default;
bool CompareAudioWorker::validateSource(const QString& path, QString* error)
{
    AudioSourceReader reader;
    return reader.open(path, AudioFormat{48000, 2, 2}, error);
}
void CompareAudioWorker::openSource(const QString& path, int rate, int channels, quint64 generation)
{
    m_running = false; m_generation = generation; m_reader->close(); if (m_buffer) m_buffer->clear();
    m_format = {rate, channels, 2}; QString error;
    if (path.isEmpty() || !m_reader->open(path, m_format, &error)) {
        emit sourceUnavailable(error.isEmpty() ? QStringLiteral("No comparison audio source.") : error, generation); return;
    }
    emit sourceReady(m_reader->durationUs(), m_reader->startTimeUs(), generation);
}
void CompareAudioWorker::closeSource(quint64 generation)
{ m_generation = generation; m_running = false; m_reader->close(); if (m_buffer) m_buffer->clear(); }
void CompareAudioWorker::startAt(qint64 providerTimeUs, qint64 masterEpochUs, quint64 generation)
{
    if (generation != m_generation || !m_reader->isOpen() || m_shuttingDown) {
        emit audioPrimed(0, masterEpochUs, generation); return;
    }
    if (m_buffer) m_buffer->clear();
    m_cursorUs = std::max<qint64>(m_reader->startTimeUs(), providerTimeUs);
    m_masterEpochUs = masterEpochUs; m_running = true; m_primed = false; pump();
}
void CompareAudioWorker::stop(quint64 generation)
{ m_generation = generation; m_running = false; if (m_buffer) m_buffer->clear(); }
void CompareAudioWorker::pump()
{
    if (!m_running || m_shuttingDown || !m_reader->isOpen() || !m_buffer) return;
    const qint64 bufferedMs = m_format.bytesToMicroseconds(m_buffer->bytesAvailable()) / 1000;
    if (bufferedMs < kTargetMs) {
        QByteArray pcm; qint64 actual = m_cursorUs; QString error;
        if (m_reader->readRange(m_cursorUs, kChunkUs, pcm, &actual, &error) && !pcm.isEmpty()) {
            const qint64 written = m_buffer->write(pcm.constData(), pcm.size(), actual);
            m_cursorUs = actual + m_format.bytesToMicroseconds(written);
        } else m_running = false;
    }
    const qint64 nowMs = m_format.bytesToMicroseconds(m_buffer->bytesAvailable()) / 1000;
    if (!m_primed && (nowMs >= kPrimeMs || !m_running)) {
        m_primed = true; emit audioPrimed(static_cast<int>(nowMs), m_masterEpochUs, m_generation);
    }
    if (m_running) QTimer::singleShot(10, this, &CompareAudioWorker::pump);
}
void CompareAudioWorker::shutdown()
{ m_shuttingDown = true; m_running = false; if (m_buffer) m_buffer->clear(); m_reader->close(); }
}
