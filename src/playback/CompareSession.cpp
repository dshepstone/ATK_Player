#include "playback/CompareSession.h"
#include <algorithm>
#include <limits>

namespace atk::playback {

CompareSession::CompareSession(QObject* parent) : QObject(parent) {}
void CompareSession::bumpGeneration() { ++m_generation; }
void CompareSession::setActive(bool active) { if (m_active == active) return; m_active = active; bumpGeneration(); emit activeChanged(active); }

bool CompareSession::setSources(const QUuid& a, const QUuid& b)
{
    if (a.isNull() || b.isNull() || a == b) return false;
    if (m_sourceAId == a && m_sourceBId == b) return true;
    const bool bChanged = m_sourceBId != b;
    m_sourceAId = a; m_sourceBId = b;
    if (bChanged && m_sourceBOffsetUs != 0) { m_sourceBOffsetUs = 0; emit offsetChanged(0); }
    bumpGeneration(); emit sourcesChanged(a, b); return true;
}
void CompareSession::setLayout(CompareLayout value) { if (m_layout == value) return; m_layout = value; emit layoutChanged(value); }
void CompareSession::setActivePane(ComparePane value) { if (m_activePane == value) return; m_activePane = value; emit activePaneChanged(value); }
void CompareSession::setSourceBOffsetUs(qint64 value) { if (m_sourceBOffsetUs == value) return; m_sourceBOffsetUs = value; bumpGeneration(); emit offsetChanged(value); }
void CompareSession::setAudioMode(CompareAudioMode value) { if (m_audioMode == value) return; m_audioMode = value; bumpGeneration(); emit audioModeChanged(value); }
void CompareSession::setExternalAudioPath(const QString& value) { if (m_externalAudioPath == value) return; m_externalAudioPath = value; if (m_externalAudioOffsetUs != 0) { m_externalAudioOffsetUs = 0; emit externalAudioOffsetChanged(0); } bumpGeneration(); emit externalAudioChanged(value); }
void CompareSession::setExternalAudioOffsetUs(qint64 value) { if (m_externalAudioOffsetUs == value) return; m_externalAudioOffsetUs = value; bumpGeneration(); emit externalAudioOffsetChanged(value); }
void CompareSession::setWipePosition(int value) { value = std::clamp(value, 0, 100); if (m_wipePosition == value) return; m_wipePosition = value; emit wipePositionChanged(value); }
void CompareSession::setBlendAmount(int value) { value = std::clamp(value, 0, 100); if (m_blendAmount == value) return; m_blendAmount = value; emit blendAmountChanged(value); }

qint64 CompareSession::frameTimeUs(qint64 frame, const media::FrameRate& rate)
{
    if (!rate.isValid() || frame <= 0) return 0;
    return static_cast<qint64>(static_cast<long double>(frame) * rate.denominator
                               * 1'000'000.0L / rate.numerator);
}

qint64 CompareSession::mappedTargetUs(qint64 aPts, qint64 aStart, qint64 bStart,
                                      qint64 bEnd, qint64 offset)
{
    const qint64 compareUs = std::max<qint64>(0, aPts - aStart);
    return std::clamp(bStart + compareUs + offset, bStart, std::max(bStart, bEnd));
}

qint64 CompareSession::constantRateFrameForTime(qint64 targetUs, const media::FrameRate& rate,
                                                qint64 first, qint64 last)
{
    if (!rate.isValid()) return first;
    const long double frame = static_cast<long double>(targetUs) * rate.numerator
        / (static_cast<long double>(rate.denominator) * 1'000'000.0L);
    return std::clamp(static_cast<qint64>(frame), first, std::max(first, last));
}

int CompareSession::frameForPts(qint64 targetUs, const QVector<qint64>& pts)
{
    if (pts.isEmpty()) return -1;
    const auto it = std::upper_bound(pts.cbegin(), pts.cend(), targetUs);
    if (it == pts.cbegin()) return 0;
    return static_cast<int>(std::distance(pts.cbegin(), it) - 1);
}

} // namespace atk::playback
