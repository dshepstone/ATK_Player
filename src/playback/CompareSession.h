#pragma once

#include "media/MediaMetadata.h"
#include <QObject>
#include <QUuid>
#include <QVector>
#include <cstdint>

namespace atk::playback {

enum class CompareLayout { SideBySide, Stacked };
enum class ComparePane { A, B };
enum class CompareAudioMode { SourceA, SourceB, External };

class CompareSession : public QObject {
    Q_OBJECT
public:
    explicit CompareSession(QObject* parent = nullptr);
    bool isActive() const { return m_active; }
    void setActive(bool active);
    QUuid sourceAId() const { return m_sourceAId; }
    QUuid sourceBId() const { return m_sourceBId; }
    bool setSources(const QUuid& sourceAId, const QUuid& sourceBId);
    CompareLayout layout() const { return m_layout; }
    void setLayout(CompareLayout layout);
    ComparePane activePane() const { return m_activePane; }
    void setActivePane(ComparePane pane);
    qint64 sourceBOffsetUs() const { return m_sourceBOffsetUs; }
    void setSourceBOffsetUs(qint64 offsetUs);
    quint64 generation() const { return m_generation; }
    CompareAudioMode audioMode() const { return m_audioMode; }
    void setAudioMode(CompareAudioMode mode);
    QString externalAudioPath() const { return m_externalAudioPath; }
    void setExternalAudioPath(const QString& path);
    qint64 externalAudioOffsetUs() const { return m_externalAudioOffsetUs; }
    void setExternalAudioOffsetUs(qint64 value);

    static qint64 frameTimeUs(qint64 frame, const media::FrameRate& rate);
    static qint64 mappedTargetUs(qint64 sourceAPtsUs, qint64 sourceARangeStartUs,
                                 qint64 sourceBRangeStartUs, qint64 sourceBRangeEndUs,
                                 qint64 sourceBOffsetUs = 0);
    static qint64 constantRateFrameForTime(qint64 targetUs, const media::FrameRate& rate,
                                           qint64 firstFrame, qint64 lastFrame);
    static int frameForPts(qint64 targetUs, const QVector<qint64>& presentationTimesUs);

signals:
    void activeChanged(bool active);
    void sourcesChanged(const QUuid& sourceAId, const QUuid& sourceBId);
    void layoutChanged(atk::playback::CompareLayout layout);
    void activePaneChanged(atk::playback::ComparePane pane);
    void offsetChanged(qint64 offsetUs);
    void audioModeChanged(atk::playback::CompareAudioMode mode);
    void externalAudioChanged(const QString& path);

private:
    void bumpGeneration();
    QUuid m_sourceAId;
    QUuid m_sourceBId;
    CompareLayout m_layout = CompareLayout::SideBySide;
    ComparePane m_activePane = ComparePane::A;
    qint64 m_sourceBOffsetUs = 0;
    CompareAudioMode m_audioMode = CompareAudioMode::SourceA;
    QString m_externalAudioPath;
    qint64 m_externalAudioOffsetUs = 0;
    quint64 m_generation = 0;
    bool m_active = false;
};

} // namespace atk::playback

Q_DECLARE_METATYPE(atk::playback::CompareLayout)
Q_DECLARE_METATYPE(atk::playback::ComparePane)
Q_DECLARE_METATYPE(atk::playback::CompareAudioMode)
