#include "playback/CompareVideoLane.h"
#include "media/DecoderWorker.h"
#include "media/MediaSource.h"
#include "playback/CompareSession.h"
#include <QMetaObject>
#include <QThread>
#include <algorithm>

namespace atk::playback {
CompareVideoLane::CompareVideoLane(QObject* parent)
    : QObject(parent), m_thread(new QThread(this)), m_generations(std::make_shared<media::DecodeGenerations>())
{
    m_thread->setObjectName(QStringLiteral("ATK comparison video B"));
    m_worker = new media::DecoderWorker(nullptr, m_generations);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &CompareVideoLane::requestOpen, m_worker, &media::DecoderWorker::openMedia);
    connect(this, &CompareVideoLane::requestClose, m_worker, &media::DecoderWorker::closeMedia);
    connect(this, &CompareVideoLane::requestFrame, m_worker, &media::DecoderWorker::requestFrame);
    connect(m_worker, &media::DecoderWorker::mediaOpened, this, &CompareVideoLane::onOpened);
    connect(m_worker, &media::DecoderWorker::mediaOpenFailed, this, &CompareVideoLane::onOpenFailed);
    connect(m_worker, &media::DecoderWorker::frameReady, this, &CompareVideoLane::onFrameReady);
    connect(m_worker, &media::DecoderWorker::frameFailed, this, [this](qint64, const QString& message, quint64 generation) {
        if (!m_generations->isCurrentRequest(generation)) return;
        m_requestInFlight = false;
        emit errorOccurred(message);
    });
    m_thread->start();
}
CompareVideoLane::~CompareVideoLane()
{
    m_generations->bumpSource();
    disconnect(m_worker, nullptr, this, nullptr);
    QMetaObject::invokeMethod(m_worker, &media::DecoderWorker::shutdown, Qt::BlockingQueuedConnection);
    m_thread->quit();
    if (!m_thread->wait(5000)) { m_thread->terminate(); m_thread->wait(1000); }
}
void CompareVideoLane::open(const QUuid& id, const std::shared_ptr<media::MediaSource>& source,
                            qint64 start, qint64 end)
{
    close();
    if (id.isNull() || !source) return;
    m_sourceId = id; m_rangeStartFrame = std::max<qint64>(0, start); m_rangeEndFrame = std::max(m_rangeStartFrame, end);
    const quint64 generation = m_generations->bumpSource();
    m_cache.setSourceGeneration(generation);
    emit loadingChanged(true);
    emit requestOpen(source->filePath(), generation);
}
void CompareVideoLane::close()
{
    m_generations->bumpSource(); emit requestClose(); m_cache.clear(); m_metadata = {}; m_sourceId = {};
    m_presentedPtsUs = -1; m_presentedFrameIndex = -1; m_presentedFrame = {};
    m_pendingFrame = -1; m_requestInFlight = false;
    if (m_ready) { m_ready = false; emit readyChanged(false); }
}
void CompareVideoLane::onOpened(const media::MediaMetadata& metadata, quint64 sourceGeneration)
{
    if (!m_generations->isCurrentSource(sourceGeneration)) return;
    m_metadata = metadata;
    const qint64 last = std::max<qint64>(0, metadata.effectiveFrameCount() - 1);
    m_rangeStartFrame = std::clamp(m_rangeStartFrame, qint64(0), last);
    m_rangeEndFrame = std::clamp(m_rangeEndFrame, m_rangeStartFrame, last);
    m_ready = true; emit loadingChanged(false); emit readyChanged(true); synchronizeTo(m_latestTargetUs);
}
void CompareVideoLane::onOpenFailed(const QString& message, quint64 generation)
{
    if (!m_generations->isCurrentSource(generation)) return;
    emit loadingChanged(false); emit errorOccurred(message);
}
qint64 CompareVideoLane::targetFrame(qint64 targetUs) const
{
    return CompareSession::constantRateFrameForTime(targetUs, m_metadata.frameRate, m_rangeStartFrame, m_rangeEndFrame);
}
void CompareVideoLane::synchronizeTo(qint64 targetUs)
{
    m_latestTargetUs = std::max<qint64>(0, targetUs);
    if (!m_ready) return;
    requestTargetFrame(targetFrame(m_latestTargetUs));
}

void CompareVideoLane::synchronizeToSourceFrame(
    const media::VideoFrame& sourceAFrame, const media::MediaMetadata& sourceAMetadata,
    qint64 sourceARangeStartFrame, qint64 sourceBOffsetUs)
{
    if (!m_ready || !sourceAFrame.isValid()) return;
    requestTargetFrame(CompareSession::constantRateFrameForSourcePts(
        sourceAFrame.ptsTicks, sourceAMetadata.videoTimeBase,
        sourceAMetadata.videoStartTime,
        sourceARangeStartFrame, sourceAMetadata.frameRate,
        m_rangeStartFrame, m_rangeEndFrame, m_metadata.frameRate, sourceBOffsetUs));
}

void CompareVideoLane::requestTargetFrame(qint64 frame)
{
    if (m_requestInFlight && frame != m_pendingFrame) {
        // Mapping changes are authoritative immediately. Supersede the old
        // decode so its frame cannot flash after an offset edit.
        m_generations->bumpRequest();
        m_requestInFlight = false;
        m_pendingFrame = -1;
    }
    if (m_requestInFlight) return;
    if (frame == m_pendingFrame && m_presentedPtsUs >= 0) return;
    if (const media::VideoFrame* cached = m_cache.find(frame)) {
        m_pendingFrame = frame; m_presentedPtsUs = cached->ptsUs;
        m_presentedFrameIndex = cached->frameIndex; m_presentedFrame = *cached;
        emit frameChanged(*cached); return;
    }
    m_pendingFrame = frame;
    m_requestInFlight = true;
    emit requestFrame(frame, m_generations->bumpRequest());
}
void CompareVideoLane::onFrameReady(const media::VideoFrame& frame, quint64 requestGeneration)
{
    if (!m_generations->isCurrentSource(frame.sourceGeneration)
        || !m_generations->isCurrentRequest(requestGeneration) || frame.frameIndex != m_pendingFrame) return;
    m_requestInFlight = false;
    m_cache.insert(frame); m_presentedPtsUs = frame.ptsUs;
    m_presentedFrameIndex = frame.frameIndex; m_presentedFrame = frame; emit frameChanged(frame);
}
} // namespace atk::playback
