#include "annotation/AnnotationStore.h"

#include <algorithm>
#include <utility>

namespace atk::annotation {

AnnotationStore::AnnotationStore(QObject* parent) : QObject(parent) {}
AnnotationStore::~AnnotationStore() = default;

void AnnotationStore::setAnnotations(SourceAnnotations annotations)
{
    m_annotations = std::move(annotations);
    m_nextId = m_annotations.maxItemId() + 1;
    const bool hadHistory = canUndo() || canRedo();
    m_undo.clear();
    m_redo.clear();
    emit annotationsReset();
    if (hadHistory) emit historyChanged();
}

quint64 AnnotationStore::allocateId() { return m_nextId++; }

quint64 AnnotationStore::addItem(int64_t frame, int64_t mediaTimeUs, AnnotationItem item)
{
    if (frame < 0 || !isValidItem(item)) return 0;

    FrameAnnotation before = m_annotations.frame(frame);
    if (before.items.size() >= AnnotationLimits::kMaxItemsPerFrame) return 0;
    if (before.isEmpty() && m_annotations.frameCount() >= AnnotationLimits::kMaxAnnotatedFrames)
        return 0;

    FrameAnnotation after = before;
    if (after.isEmpty()) after.mediaTimeUs = mediaTimeUs;
    item.id = allocateId();
    after.items.append(std::move(item));
    const quint64 id = after.items.constLast().id;
    commit({{frame, std::move(before), std::move(after)}});
    return id;
}

bool AnnotationStore::removeItem(int64_t frame, quint64 itemId)
{
    const FrameAnnotation* found = m_annotations.find(frame);
    if (!found) return false;
    const auto it = std::find_if(found->items.cbegin(), found->items.cend(),
                                 [itemId](const AnnotationItem& i) { return i.id == itemId; });
    if (it == found->items.cend()) return false;

    FrameAnnotation before = *found;
    FrameAnnotation after = before;
    after.items.remove(int(it - found->items.cbegin()));
    commit({{frame, std::move(before), std::move(after)}});
    return true;
}

quint64 AnnotationStore::removeLastItemAmong(int64_t frame, const QVector<quint64>& ids)
{
    const FrameAnnotation* found = m_annotations.find(frame);
    if (!found) return 0;
    for (auto it = found->items.crbegin(); it != found->items.crend(); ++it) {
        if (ids.contains(it->id)) {
            const quint64 id = it->id;
            return removeItem(frame, id) ? id : 0;
        }
    }
    return 0;
}

bool AnnotationStore::clearFrame(int64_t frame)
{
    const FrameAnnotation* found = m_annotations.find(frame);
    if (!found) return false;
    FrameAnnotation before = *found;
    FrameAnnotation after;
    after.frame = frame;
    commit({{frame, std::move(before), std::move(after)}});
    return true;
}

bool AnnotationStore::clearAll()
{
    if (m_annotations.isEmpty()) return false;
    Edit edit;
    for (const FrameAnnotation& frame : m_annotations.frames()) {
        FrameChange change;
        change.frame = frame.frame;
        change.before = frame;
        change.after.frame = frame.frame;
        edit.append(std::move(change));
    }
    commit(std::move(edit));
    return true;
}

void AnnotationStore::commit(Edit edit)
{
    apply(edit, true);
    m_undo.append(std::move(edit));
    if (m_undo.size() > kUndoLimit) m_undo.removeFirst();
    m_redo.clear();
    emit annotationsEdited();
    emit historyChanged();
}

void AnnotationStore::apply(const Edit& edit, bool forward)
{
    for (const FrameChange& change : edit) {
        m_annotations.setFrame(forward ? change.after : change.before);
        // setFrame() of an empty annotation removes it, keyed by its own frame.
        emit frameChanged(change.frame);
    }
}

bool AnnotationStore::undo()
{
    if (m_undo.isEmpty()) return false;
    Edit edit = m_undo.takeLast();
    apply(edit, false);
    m_redo.append(std::move(edit));
    emit annotationsEdited();
    emit historyChanged();
    return true;
}

bool AnnotationStore::redo()
{
    if (m_redo.isEmpty()) return false;
    Edit edit = m_redo.takeLast();
    apply(edit, true);
    m_undo.append(std::move(edit));
    emit annotationsEdited();
    emit historyChanged();
    return true;
}

void AnnotationStore::clearHistory()
{
    if (!canUndo() && !canRedo()) return;
    m_undo.clear();
    m_redo.clear();
    emit historyChanged();
}

} // namespace atk::annotation
