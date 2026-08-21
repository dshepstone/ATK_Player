#include "project/Project.h"

#include "core/Logging.h"

#include <utility>
#include <algorithm>

namespace atk::project {

Project::Project(QObject* parent)
    : QObject(parent)
    , m_name(QStringLiteral("Untitled"))
{
}

Project::~Project() = default;

void Project::setName(const QString& name)
{
    if (m_name == name) {
        return;
    }
    m_name = name;
    emit nameChanged(m_name);
    setModified(true);
}

void Project::setFilePath(const QString& path)
{
    m_filePath = path;
}

void Project::setModified(bool modified)
{
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    emit modifiedChanged(m_modified);
}

bool Project::isValidIndex(int index) const
{
    return index >= 0 && index < m_entries.size();
}

int Project::addSource(std::shared_ptr<media::MediaSource> source)
{
    if (!source) {
        return -1;
    }

    SourceEntry entry;
    entry.displayName = source->displayName();
    entry.source = std::move(source);
    m_entries.push_back(std::move(entry));

    const int index = static_cast<int>(m_entries.size()) - 1;
    qCInfo(log::project) << "Added source at playlist index" << index;

    emit entriesChanged();
    setModified(true);

    if (m_activeIndex < 0) {
        setActiveIndex(index);
    }
    return index;
}

void Project::removeSourceAt(int index)
{
    if (!isValidIndex(index)) {
        return;
    }

    m_entries.remove(index);

    // Shift the indices that pointed past the removed entry.
    auto adjust = [index](int& stored) {
        if (stored == index)      stored = -1;
        else if (stored > index)  --stored;
    };
    adjust(m_activeIndex);
    adjust(m_compareA);
    adjust(m_compareB);

    emit entriesChanged();
    emit activeIndexChanged(m_activeIndex);
    emit compareAssignmentChanged();
    setModified(true);
}

void Project::moveSource(int fromIndex, int toIndex)
{
    if (!isValidIndex(fromIndex) || !isValidIndex(toIndex) || fromIndex == toIndex) {
        return;
    }
    const QUuid activeId = currentSourceId();
    m_entries.move(fromIndex, toIndex);
    m_activeIndex = indexForId(activeId);
    emit entriesChanged();
    emit activeIndexChanged(m_activeIndex);
    setModified(true);
}

QUuid Project::currentSourceId() const
{
    return isValidIndex(m_activeIndex) ? m_entries.at(m_activeIndex).id : QUuid{};
}

int Project::indexForId(const QUuid& id) const
{
    if (id.isNull()) return -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == id) return i;
    return -1;
}

void Project::replace(QString name, QString filePath, QVector<SourceEntry> entries,
                      const QUuid& currentSourceId)
{
    m_name = std::move(name);
    m_filePath = std::move(filePath);
    m_entries = std::move(entries);
    m_activeIndex = indexForId(currentSourceId);
    if (m_activeIndex < 0 && !m_entries.isEmpty()) m_activeIndex = 0;
    m_compareA = -1;
    m_compareB = -1;
    emit nameChanged(m_name);
    emit entriesChanged();
    emit activeIndexChanged(m_activeIndex);
    emit compareAssignmentChanged();
    setModified(false);
}

bool Project::relinkSource(const QUuid& id, std::shared_ptr<media::MediaSource> source,
                           int64_t replacementFrameCount)
{
    const int index = indexForId(id);
    if (index < 0 || !source || replacementFrameCount <= 0) return false;
    SourceEntry& entry = m_entries[index];
    entry.source = std::move(source);
    entry.storedPath.clear();
    entry.displayName = entry.source->displayName();
    entry.missing = false;
    entry.availability = entry.source->metadata().isValid()
        ? SourceAvailability::Ready : SourceAvailability::Unknown;
    entry.availabilityError.clear();
    const int64_t last = replacementFrameCount - 1;
    QVector<timeline::Bookmark> retained;
    for (timeline::Bookmark bookmark : entry.bookmarks) {
        if (bookmark.frame > last) continue;
        if (bookmark.endFrame > last) bookmark.endFrame = last;
        if (bookmark.type == timeline::BookmarkType::Range && bookmark.endFrame == bookmark.frame)
            bookmark.type = timeline::BookmarkType::Point;
        retained.append(std::move(bookmark));
    }
    entry.bookmarks = std::move(retained);
    if (entry.playbackRange.enabled) {
        entry.playbackRange.startFrame = std::min(entry.playbackRange.startFrame, last);
        entry.playbackRange.endFrame = std::clamp(entry.playbackRange.endFrame,
                                                   entry.playbackRange.startFrame, last);
    }
    emit entriesChanged();
    setModified(true);
    return true;
}

bool Project::beginProbe(const QUuid& id, const QString& path, quint64 token)
{
    const int index = indexForId(id);
    if (index < 0 || !m_entries[index].source || m_entries[index].source->filePath() != path) return false;
    auto& entry = m_entries[index];
    entry.probeToken = token; entry.availability = SourceAvailability::Probing;
    entry.availabilityError.clear(); emit entriesChanged(); return true;
}

bool Project::applyProbeResult(const QUuid& id, const QString& path, quint64 token,
                               const media::MediaMetadata& metadata, const QString& error,
                               bool missing)
{
    const int index = indexForId(id);
    if (index < 0) return false;
    auto& entry = m_entries[index];
    if (!entry.source || entry.source->filePath() != path || entry.probeToken != token) return false;
    entry.missing = missing;
    entry.availabilityError = error;
    if (missing) entry.availability = SourceAvailability::Missing;
    else if (!error.isEmpty() || !metadata.isValid()) entry.availability = SourceAvailability::Error;
    else { entry.availability = SourceAvailability::Ready; entry.source->setMetadata(metadata); }
    emit entriesChanged();
    return true;
}

void Project::clear()
{
    if (m_entries.isEmpty() && m_activeIndex < 0) {
        return;
    }
    m_entries.clear();
    m_activeIndex = -1;
    m_compareA = -1;
    m_compareB = -1;
    emit entriesChanged();
    emit activeIndexChanged(m_activeIndex);
    emit compareAssignmentChanged();
    setModified(true);
}

void Project::setActiveIndex(int index)
{
    const int clamped = isValidIndex(index) ? index : -1;
    if (m_activeIndex == clamped) {
        return;
    }
    m_activeIndex = clamped;
    emit activeIndexChanged(m_activeIndex);
}

void Project::setCompareIndex(CompareSlot slot, int playlistIndex)
{
    const int clamped = isValidIndex(playlistIndex) ? playlistIndex : -1;
    switch (slot) {
    case CompareSlot::A:
        if (m_compareA == clamped) return;
        m_compareA = clamped;
        break;
    case CompareSlot::B:
        if (m_compareB == clamped) return;
        m_compareB = clamped;
        break;
    case CompareSlot::None:
        return;
    }
    emit compareAssignmentChanged();
    setModified(true);
}

} // namespace atk::project
