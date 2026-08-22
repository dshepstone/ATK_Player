#include "export/ExportJob.h"
#include "export/ExportRenderer.h"
#include "export/FFmpegExporter.h"
#include "media/AudioSourceReader.h"
#include "playback/CompareSession.h"
#include <QFile>
#include <QThread>
#include <algorithm>

namespace atk::exporter {

class ExportWorker final : public QObject {
    Q_OBJECT
public:
    ExportWorker(ExportSpec spec, std::shared_ptr<std::atomic_bool> cancelled)
        : m_spec(std::move(spec)), m_cancelled(std::move(cancelled)) {}
public slots:
    void run()
    {
        emit started(); emit statusChanged(tr("Preparing…"));
        const QString validation = m_spec.validate();
        if (!validation.isEmpty()) { emit failed(validation); return; }
        ExportRenderer renderer; QString error;
        if (!renderer.open(m_spec, &error)) { emit failed(cleanError(error)); return; }
        const bool wantAudio = audioAvailable();
        FFmpegExporter encoder;
        if (!encoder.open(m_spec, wantAudio, &error)) { encoder.discard(); emit failed(cleanError(error)); return; }
        const qint64 total = m_spec.frameCount();
        for (qint64 n = 0; n < total; ++n) {
            if (isCancelled()) { encoder.discard(); emit cancelled(); return; }
            const qint64 index = m_spec.sourceA.rangeStartFrame + n;
            RenderedExportFrame frame;
            if (!renderer.render(index, frame, &error, [this]{ return isCancelled(); })) {
                encoder.discard();
                if (isCancelled()) emit cancelled(); else emit failed(cleanError(error));
                return;
            }
            if (!encoder.encodeVideo(frame.image, n, &error)) {
                encoder.discard(); emit failed(cleanError(error)); return;
            }
            emit statusChanged(tr("Rendering frame %1 / %2…").arg(n + 1).arg(total));
            emit progress(static_cast<int>((n + 1) * 90 / total), n + 1, total);
        }
        if (wantAudio) {
            emit statusChanged(tr("Encoding audio…"));
            // CFR output duration comes from the exact rational frame grid,
            // not rounded native source PTS. This keeps AAC trim/padding on the
            // same N-frame timeline as video (not the N-1 final-frame PTS).
            const auto rate = m_spec.sourceA.metadata.frameRate;
            const qint64 audioSamples = (total * rate.denominator * m_spec.audioSampleRate
                + rate.numerator / 2) / rate.numerator;
            const qint64 durationUs = (audioSamples * 1'000'000
                + m_spec.audioSampleRate / 2) / m_spec.audioSampleRate;
            QByteArray pcm;
            if (!renderAudio(durationUs, audioSamples, pcm, &error)
                || !encoder.encodeAudio(pcm, &error)) {
                encoder.discard(); emit failed(cleanError(error)); return;
            }
        }
        if (isCancelled()) { encoder.discard(); emit cancelled(); return; }
        emit statusChanged(tr("Finalizing…"));
        if (!encoder.finish(&error) || !replaceFinal(encoder.temporaryPath(), &error)) {
            encoder.discard(); emit failed(cleanError(error)); return;
        }
        emit progress(100, total, total); emit completed(m_spec.outputPath);
    }
signals:
    void started();
    void progress(int percent, qint64 frame, qint64 total);
    void statusChanged(const QString& status);
    void completed(const QString& outputPath);
    void cancelled();
    void failed(const QString& message);
private:
    bool isCancelled() const { return m_cancelled->load(); }
    bool audioAvailable() const
    {
        if (!m_spec.comparison || m_spec.audioMode == playback::CompareAudioMode::SourceA)
            return m_spec.sourceA.metadata.hasAudio;
        if (m_spec.audioMode == playback::CompareAudioMode::SourceB) return m_spec.sourceB.metadata.hasAudio;
        return !m_spec.externalAudioPath.isEmpty();
    }
    bool renderAudio(qint64 durationUs, qint64 sampleCount, QByteArray& output, QString* error)
    {
        QString path; qint64 sourceStartUs = 0;
        if (!m_spec.comparison || m_spec.audioMode == playback::CompareAudioMode::SourceA) {
            path = m_spec.sourceA.path;
            sourceStartUs = playback::CompareSession::frameTimeUs(
                m_spec.sourceA.rangeStartFrame, m_spec.sourceA.metadata.frameRate);
        } else if (m_spec.audioMode == playback::CompareAudioMode::SourceB) {
            path = m_spec.sourceB.path;
            sourceStartUs = playback::CompareSession::frameTimeUs(
                m_spec.sourceB.rangeStartFrame, m_spec.sourceB.metadata.frameRate)
                + m_spec.sourceBOffsetUs;
        } else {
            path = m_spec.externalAudioPath; sourceStartUs = m_spec.externalAudioOffsetUs;
        }
        media::AudioFormat format{m_spec.audioSampleRate, m_spec.audioChannels, 2};
        media::AudioSourceReader reader;
        if (!reader.open(path, format, error)) { output.clear(); return true; }
        output = QByteArray(static_cast<qsizetype>(sampleCount * format.bytesPerFrame()), '\0');
        const qint64 leadingUs = std::clamp<qint64>(-sourceStartUs, 0, durationUs);
        const qint64 readStart = std::max<qint64>(0, sourceStartUs);
        const qint64 readDuration = durationUs - leadingUs;
        QByteArray decoded; qint64 actual = 0;
        if (readDuration > 0 && !reader.readRange(readStart, readDuration, decoded, &actual, error)) return false;
        const qsizetype destination = static_cast<qsizetype>(format.microsecondsToBytes(leadingUs));
        const qsizetype count = std::min(decoded.size(), output.size() - destination);
        if (count > 0) std::copy_n(decoded.constData(), count, output.data() + destination);
        return true;
    }
    bool replaceFinal(const QString& temporary, QString* error)
    {
        const QString backup = m_spec.outputPath + QStringLiteral(".atkbackup");
        QFile::remove(backup);
        const bool existed = QFile::exists(m_spec.outputPath);
        if (existed && !QFile::rename(m_spec.outputPath, backup)) {
            if (error) *error = tr("Unable to preserve the existing destination."); return false;
        }
        if (!QFile::rename(temporary, m_spec.outputPath)) {
            if (existed) QFile::rename(backup, m_spec.outputPath);
            if (error) *error = tr("Unable to install the completed export."); return false;
        }
        QFile::remove(backup); return true;
    }
    static QString cleanError(const QString& detail)
    { return detail.isEmpty() ? tr("Export failed.") : detail.left(500); }
    ExportSpec m_spec;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

ExportJob::ExportJob(ExportSpec spec, QObject* parent)
    : QObject(parent), m_spec(std::move(spec)), m_cancelled(std::make_shared<std::atomic_bool>(false)) {}
ExportJob::~ExportJob() { cancel(); wait(); }
void ExportJob::start()
{
    if (m_running) return; m_running = true; m_cancelled->store(false);
    m_thread = new QThread(this); m_thread->setObjectName(QStringLiteral("ATK offline export"));
    m_worker = new ExportWorker(m_spec, m_cancelled); m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &ExportWorker::run);
    connect(m_worker, &ExportWorker::started, this, &ExportJob::started);
    connect(m_worker, &ExportWorker::progress, this, &ExportJob::progress);
    connect(m_worker, &ExportWorker::statusChanged, this, &ExportJob::statusChanged);
    connect(m_worker, &ExportWorker::completed, this, [this](const QString& path) {
        m_running = false; if (m_thread) m_thread->quit(); emit completed(path);
    });
    connect(m_worker, &ExportWorker::cancelled, this, [this] {
        m_running = false; if (m_thread) m_thread->quit(); emit cancelled();
    });
    connect(m_worker, &ExportWorker::failed, this, [this](const QString& message) {
        m_running = false; if (m_thread) m_thread->quit(); emit failed(message);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}
void ExportJob::cancel() { m_cancelled->store(true); }
void ExportJob::wait() { if (m_thread && m_thread->isRunning()) { m_thread->quit(); m_thread->wait(); } }

} // namespace atk::exporter

#include "ExportJob.moc"
