#pragma once

#include <cstdint>

namespace atk::timeline {

class TimelineViewport {
public:
    static constexpr int64_t kMinimumVisibleFrames = 10;

    void reset(int64_t frameCount);
    void fit() { reset(m_frameCount); }
    void zoom(double factor, int64_t anchorFrame);
    void pan(int64_t deltaFrames);
    void setRange(int64_t startFrame, int64_t endFrame);
    void ensureVisible(int64_t frame, double edgeFraction = 0.1);

    int64_t startFrame() const { return m_startFrame; }
    int64_t endFrame() const { return m_endFrame; }
    int64_t visibleFrameCount() const;
    bool contains(int64_t frame) const;
    double fractionForFrame(int64_t frame) const;
    int64_t frameAtFraction(double fraction) const;

private:
    void setWindow(int64_t start, int64_t count);

    int64_t m_frameCount = 0;
    int64_t m_startFrame = 0;
    int64_t m_endFrame = 0;
};

} // namespace atk::timeline
