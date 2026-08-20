#include "timeline/TimelineViewport.h"

#include <algorithm>
#include <cmath>

namespace atk::timeline {

void TimelineViewport::reset(int64_t frameCount)
{
    m_frameCount = std::max<int64_t>(0, frameCount);
    m_startFrame = 0;
    m_endFrame = std::max<int64_t>(0, m_frameCount - 1);
}

int64_t TimelineViewport::visibleFrameCount() const
{
    return m_frameCount > 0 ? m_endFrame - m_startFrame + 1 : 0;
}

bool TimelineViewport::contains(int64_t frame) const
{
    return m_frameCount > 0 && frame >= m_startFrame && frame <= m_endFrame;
}

double TimelineViewport::fractionForFrame(int64_t frame) const
{
    const int64_t distance = m_endFrame - m_startFrame;
    return distance > 0 ? double(frame - m_startFrame) / double(distance) : 0.0;
}

int64_t TimelineViewport::frameAtFraction(double fraction) const
{
    if (m_frameCount <= 0) return 0;
    fraction = std::clamp(fraction, 0.0, 1.0);
    return m_startFrame + static_cast<int64_t>(std::llround(
        fraction * double(m_endFrame - m_startFrame)));
}

void TimelineViewport::setWindow(int64_t start, int64_t count)
{
    if (m_frameCount <= 0) { reset(0); return; }
    count = std::clamp<int64_t>(count,
        std::min<int64_t>(kMinimumVisibleFrames, m_frameCount), m_frameCount);
    start = std::clamp<int64_t>(start, 0, m_frameCount - count);
    m_startFrame = start;
    m_endFrame = start + count - 1;
}

void TimelineViewport::zoom(double factor, int64_t anchorFrame)
{
    if (m_frameCount <= 1 || factor <= 0.0) return;
    anchorFrame = std::clamp<int64_t>(anchorFrame, m_startFrame, m_endFrame);
    const double anchorFraction = fractionForFrame(anchorFrame);
    const int64_t count = static_cast<int64_t>(std::llround(visibleFrameCount() / factor));
    const int64_t clampedCount = std::clamp<int64_t>(count,
        std::min<int64_t>(kMinimumVisibleFrames, m_frameCount), m_frameCount);
    setWindow(static_cast<int64_t>(std::llround(anchorFrame -
              anchorFraction * double(clampedCount - 1))), clampedCount);
}

void TimelineViewport::pan(int64_t deltaFrames)
{
    setWindow(m_startFrame + deltaFrames, visibleFrameCount());
}

void TimelineViewport::ensureVisible(int64_t frame, double edgeFraction)
{
    if (m_frameCount <= 0 || visibleFrameCount() >= m_frameCount) return;
    frame = std::clamp<int64_t>(frame, 0, m_frameCount - 1);
    const int64_t margin = std::max<int64_t>(1,
        static_cast<int64_t>(std::llround(visibleFrameCount() * edgeFraction)));
    if (frame < m_startFrame + margin) pan(frame - (m_startFrame + margin));
    else if (frame > m_endFrame - margin) pan(frame - (m_endFrame - margin));
}

} // namespace atk::timeline
