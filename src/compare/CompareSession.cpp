#include "compare/CompareSession.h"

#include "core/Logging.h"

#include <utility>

namespace atk::compare {

CompareSession::CompareSession(QObject* parent)
    : QObject(parent)
{
}

CompareSession::~CompareSession() = default;

void CompareSession::setSourceA(std::shared_ptr<media::MediaSource> source)
{
    m_sourceA = std::move(source);
    emit sourcesChanged();
}

void CompareSession::setSourceB(std::shared_ptr<media::MediaSource> source)
{
    m_sourceB = std::move(source);
    emit sourcesChanged();
}

void CompareSession::setLayout(CompareLayout layout)
{
    if (m_layout == layout) {
        return;
    }
    m_layout = layout;
    emit layoutChanged(m_layout);
}

void CompareSession::setOffsetA(int64_t frames)
{
    if (m_offsetA == frames) {
        return;
    }
    m_offsetA = frames;
    emit offsetsChanged();
}

void CompareSession::setOffsetB(int64_t frames)
{
    if (m_offsetB == frames) {
        return;
    }
    m_offsetB = frames;
    emit offsetsChanged();
}

void CompareSession::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    // TODO(M4): activating must not restart playback -- the master clock keeps
    // running and the second viewer joins at the current frame.
    m_active = active;
    qCInfo(log::playback) << "Compare mode" << (active ? "activated" : "deactivated");
    emit activeChanged(m_active);
}

} // namespace atk::compare
