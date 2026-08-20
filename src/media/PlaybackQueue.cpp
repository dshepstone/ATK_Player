#include "media/PlaybackQueue.h"

#include <utility>

namespace atk::media {

void PlaybackQueue::insert(VideoFrame frame)
{
    if (frame.sourceGeneration != m_sourceGeneration || !frame.isValid()) {
        return;
    }

    const int64_t index = frame.frameIndex;
    const int64_t bytes = frame.sizeInBytes();

    const auto existing = m_frames.find(index);
    if (existing != m_frames.end()) {
        m_usedBytes -= existing->second.sizeInBytes();
        m_usedBytes += bytes;
        existing->second = std::move(frame);
        return;
    }

    m_frames.emplace(index, std::move(frame));
    m_usedBytes += bytes;

    // Over budget: drop from the far end. Those are the frames furthest in the
    // future, so losing them costs a re-decode rather than a visible skip --
    // the opposite choice from dropping the frame about to be shown.
    while (m_usedBytes > m_budgetBytes && m_frames.size() > 1) {
        const auto last = std::prev(m_frames.end());
        m_usedBytes -= last->second.sizeInBytes();
        m_frames.erase(last);
    }
}

const VideoFrame* PlaybackQueue::find(int64_t frameIndex) const
{
    const auto it = m_frames.find(frameIndex);
    return it == m_frames.end() ? nullptr : &it->second;
}

int PlaybackQueue::discardUpTo(int64_t frameIndex)
{
    int discarded = 0;
    auto it = m_frames.begin();
    while (it != m_frames.end() && it->first < frameIndex) {
        m_usedBytes -= it->second.sizeInBytes();
        it = m_frames.erase(it);
        ++discarded;
    }
    return discarded;
}

int64_t PlaybackQueue::firstFrame() const
{
    return m_frames.empty() ? -1 : m_frames.begin()->first;
}

int64_t PlaybackQueue::lastFrame() const
{
    return m_frames.empty() ? -1 : std::prev(m_frames.end())->first;
}

void PlaybackQueue::clear()
{
    m_frames.clear();
    m_usedBytes = 0;
}

void PlaybackQueue::setSourceGeneration(uint64_t generation)
{
    if (m_sourceGeneration == generation) {
        return;
    }
    clear();
    m_sourceGeneration = generation;
}

} // namespace atk::media
