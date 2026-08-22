#pragma once
#include "media/DecodeGeneration.h"
#include "media/FrameCache.h"
#include "media/MediaMetadata.h"
#include <QObject>
#include <QUuid>
#include <memory>
class QThread;
namespace atk::media { class DecoderWorker; class MediaSource; }
namespace atk::playback {

class CompareVideoLane final : public QObject {
    Q_OBJECT
public:
    explicit CompareVideoLane(QObject* parent = nullptr);
    ~CompareVideoLane() override;
    void open(const QUuid& sourceId, const std::shared_ptr<media::MediaSource>& source,
              qint64 rangeStartFrame, qint64 rangeEndFrame);
    void close();
    void synchronizeTo(qint64 targetUs);
    void synchronizeToSourceFrame(const media::VideoFrame& sourceAFrame,
                                  const media::MediaMetadata& sourceAMetadata,
                                  qint64 sourceARangeStartFrame,
                                  qint64 sourceBOffsetUs);
    bool isReady() const { return m_ready; }
    QUuid sourceId() const { return m_sourceId; }
    const media::MediaMetadata& metadata() const { return m_metadata; }
    qint64 requestedTargetUs() const { return m_latestTargetUs; }
    qint64 requestedFrame() const { return m_pendingFrame; }
    qint64 presentedPtsUs() const { return m_presentedPtsUs; }
    qint64 presentedFrameIndex() const { return m_presentedFrameIndex; }
    const media::VideoFrame& presentedFrame() const { return m_presentedFrame; }
    qint64 cacheBytes() const { return m_cache.usedBytes(); }
    qint64 cacheBudgetBytes() const { return m_cache.budgetBytes(); }
signals:
    void requestOpen(const QString& path, quint64 sourceGeneration);
    void requestClose();
    void requestFrame(qint64 frameIndex, quint64 requestGeneration);
    void loadingChanged(bool loading);
    void readyChanged(bool ready);
    void frameChanged(const atk::media::VideoFrame& frame);
    void errorOccurred(const QString& message);
private:
    void onOpened(const media::MediaMetadata& metadata, quint64 sourceGeneration);
    void onOpenFailed(const QString& message, quint64 sourceGeneration);
    void onFrameReady(const media::VideoFrame& frame, quint64 requestGeneration);
    qint64 targetFrame(qint64 targetUs) const;
    void requestTargetFrame(qint64 frame);
    QThread* m_thread = nullptr;
    media::DecoderWorker* m_worker = nullptr;
    std::shared_ptr<media::DecodeGenerations> m_generations;
    media::FrameCache m_cache{64LL * 1024 * 1024};
    media::MediaMetadata m_metadata;
    QUuid m_sourceId;
    qint64 m_rangeStartFrame = 0;
    qint64 m_rangeEndFrame = 0;
    qint64 m_latestTargetUs = 0;
    qint64 m_presentedPtsUs = -1;
    qint64 m_presentedFrameIndex = -1;
    media::VideoFrame m_presentedFrame;
    qint64 m_pendingFrame = -1;
    bool m_requestInFlight = false;
    bool m_ready = false;
};
} // namespace atk::playback
