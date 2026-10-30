#include "project/Project.h"

#include "core/Logging.h"

#include <QDir>

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

void Project::setExportFolder(const QString& folder)
{
    const QString cleaned = folder.isEmpty() ? QString() : QDir::cleanPath(folder);
    if (m_exportFolder == cleaned) return;
    m_exportFolder = cleaned;
    setModified(true);
}

void Project::setExportBurnIns(const std::optional<exporter::ExportBurnIns>& burnIns)
{
    if (m_exportBurnIns == burnIns) return;
    m_exportBurnIns = burnIns;
    setModified(true);
}

namespace {
/// Trimmed, without blanks or repeats (compared ignoring case), in order.
QStringList cleanedCategories(const QStringList& categories)
{
    QStringList cleaned;
    for (const QString& name : categories) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed, Qt::CaseInsensitive)) cleaned.append(trimmed);
    }
    return cleaned;
}
} // namespace

void Project::setCategories(const QStringList& categories)
{
    const QStringList cleaned = cleanedCategories(categories);
    bool clipsChanged = false;
    for (SourceEntry& entry : m_entries) {
        if (!entry.category.isEmpty() && !cleaned.contains(entry.category)) {
            entry.category.clear();
            clipsChanged = true;
        }
    }
    if (cleaned == m_categories && !clipsChanged) return;
    m_categories = cleaned;
    emit entriesChanged();
    setModified(true);
}

bool Project::setClipCategory(const QUuid& id, const QString& category)
{
    const int index = indexForId(id);
    QString name = category.trimmed();
    // The spelling already in the list wins over a different case.
    for (const QString& existing : std::as_const(m_categories))
        if (existing.compare(name, Qt::CaseInsensitive) == 0) name = existing;
    if (index < 0 || m_entries[index].category == name) return false;
    if (!name.isEmpty() && !m_categories.contains(name)) m_categories.append(name);
    m_entries[index].category = name;
    emit entriesChanged();
    setModified(true);
    return true;
}

bool Project::reorder(const QVector<QUuid>& order)
{
    if (order.size() != m_entries.size()) return false;
    QVector<SourceEntry> reordered;
    reordered.reserve(m_entries.size());
    for (const QUuid& id : order) {
        const int index = indexForId(id);
        if (index < 0 || std::any_of(reordered.cbegin(), reordered.cend(),
                                     [&id](const SourceEntry& entry) { return entry.id == id; }))
            return false;
        reordered.append(m_entries.at(index));
    }
    bool same = true;
    for (int i = 0; i < m_entries.size() && same; ++i) same = m_entries.at(i).id == reordered.at(i).id;
    if (same) return true;
    const QUuid activeId = currentSourceId();
    const QUuid compareAId = isValidIndex(m_compareA) ? m_entries.at(m_compareA).id : QUuid{};
    const QUuid compareBId = isValidIndex(m_compareB) ? m_entries.at(m_compareB).id : QUuid{};
    m_entries = std::move(reordered);
    m_activeIndex = indexForId(activeId);
    m_compareA = indexForId(compareAId);
    m_compareB = indexForId(compareBId);
    emit entriesChanged();
    emit activeIndexChanged(m_activeIndex);
    emit compareAssignmentChanged();
    setModified(true);
    return true;
}

void Project::setModified(bool modified)
{
    // Not modified means "matches its file": remember that content.
    if (!modified) {
        m_savedContent = reviewContent();
        m_savedExportFolder = m_exportFolder;
        m_savedExportBurnIns = m_exportBurnIns;
        m_savedCategories = m_categories;
    }
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    emit modifiedChanged(m_modified);
}

QVector<Project::ReviewContent> Project::reviewContent() const
{
    QVector<ReviewContent> content;
    content.reserve(m_entries.size());
    for (const SourceEntry& entry : m_entries)
        content.append({entry.source ? entry.source->filePath() : entry.storedPath, entry.bookmarks,
                        entry.annotations, entry.frameOffset, entry.category});
    return content;
}

bool Project::needsSavePrompt() const
{
    // Only ask when there is something of the user's to lose. Watching,
    // scrubbing or zooming the timeline never counts.
    if (!m_modified) return false;
    const QVector<ReviewContent> now = reviewContent();
    if (!m_filePath.isEmpty()) // differs from the file
        return now != m_savedContent || m_exportFolder != m_savedExportFolder
            || m_exportBurnIns != m_savedExportBurnIns || m_categories != m_savedCategories;
    if (now.size() > 1) return true; // a playlist
    return std::any_of(now.cbegin(), now.cend(), [](const ReviewContent& entry) {
        return !entry.bookmarks.isEmpty() || !entry.annotations.isEmpty() || entry.frameOffset != 0;
    });
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
                      const QUuid& currentSourceId, QString exportFolder,
                      std::optional<exporter::ExportBurnIns> exportBurnIns, QStringList categories)
{
    m_name = std::move(name);
    m_filePath = std::move(filePath);
    m_exportFolder = exportFolder.isEmpty() ? QString() : QDir::cleanPath(exportFolder);
    m_exportBurnIns = std::move(exportBurnIns);
    m_categories = cleanedCategories(categories);
    m_entries = std::move(entries);
    // A clip's category is always one of the list's.
    for (SourceEntry& entry : m_entries) {
        if (entry.category.isEmpty()) continue;
        bool known = false;
        for (const QString& existing : std::as_const(m_categories)) {
            if (existing.compare(entry.category, Qt::CaseInsensitive) == 0) {
                entry.category = existing;
                known = true;
                break;
            }
        }
        if (!known) m_categories.append(entry.category);
    }
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

QString clipStatusKey(ClipStatus status)
{
    switch (status) {
    case ClipStatus::NotReviewed: return QStringLiteral("notReviewed");
    case ClipStatus::InReview: return QStringLiteral("inReview");
    case ClipStatus::NeedsChanges: return QStringLiteral("needsChanges");
    case ClipStatus::Approved: return QStringLiteral("approved");
    }
    return QStringLiteral("notReviewed");
}

ClipStatus clipStatusFromKey(const QString& key)
{
    for (ClipStatus status : {ClipStatus::InReview, ClipStatus::NeedsChanges, ClipStatus::Approved})
        if (key == clipStatusKey(status)) return status;
    return ClipStatus::NotReviewed;
}

bool Project::setDisplayName(const QUuid& id, const QString& name)
{
    const int index = indexForId(id);
    if (index < 0 || name.trimmed().isEmpty() || m_entries[index].displayName == name) return false;
    m_entries[index].displayName = name;
    emit entriesChanged();
    setModified(true);
    return true;
}

bool Project::setClipStatus(const QUuid& id, ClipStatus status)
{
    const int index = indexForId(id);
    if (index < 0 || m_entries[index].status == status) return false;
    m_entries[index].status = status;
    emit clipStatusChanged(id);
    setModified(true);
    return true;
}

bool Project::setAnnotations(const QUuid& id, annotation::SourceAnnotations annotations)
{
    const int index = indexForId(id);
    if (index < 0) return false;
    m_entries[index].annotations = std::move(annotations);
    emit annotationsChanged(id);
    setModified(true);
    return true;
}

bool Project::relinkSource(const QUuid& id, std::shared_ptr<media::MediaSource> source,
                           int64_t replacementFrameCount)
{
    const int index = indexForId(id);
    if (index < 0 || !source || replacementFrameCount <= 0) return false;
    SourceEntry& entry = m_entries[index];
    // The hold and rate are review state, like the bookmarks: a still relinked
    // to a re-rendered still keeps the extent its bookmarks were made against,
    // and a relinked sequence keeps its rate (its range is the replacement's).
    if (entry.source && entry.source->isStillImage() && source->isStillImage()) {
        source->setImageOptions(entry.source->imageOptions());
    } else if (entry.source && entry.source->isImageSequence() && source->isImageSequence()) {
        media::ImageSourceOptions options = source->imageOptions();
        options.frameRate = entry.source->imageOptions().frameRate;
        source->setImageOptions(options);
    }
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
    entry.annotations.trimTo(last);
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
    // A clip opened on its own starts from the default export choices, not
    // the ones of the review it replaces.
    m_exportFolder.clear();
    m_exportBurnIns.reset();
    m_categories.clear();
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
