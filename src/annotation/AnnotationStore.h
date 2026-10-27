#pragma once

#include "annotation/AnnotationEraser.h"
#include "annotation/AnnotationTypes.h"

#include <QObject>
#include <QVector>

#include <functional>

namespace atk::annotation {

/// The editing session for one source's annotations: add, erase, clear, undo
/// and redo, with change signals.
///
/// Plain QObject with no widgets, like TimelineModel, so the viewer overlay,
/// the notes panel, the exporters and later the external API all drive the same
/// object. It holds data and history only -- it never decodes or paints.
///
/// History is snapshot-based: every edit records the affected frames before and
/// after, so undo and redo are exact whatever the edit was, and cheap because
/// QVector shares storage until written.
///
/// Persistence is not here. The owner loads a source's data with setAnnotations
/// and writes it back through `annotations()` when `annotationsEdited` fires;
/// loading clears history, since an undo across a project load would resurrect
/// data from a different session.
class AnnotationStore : public QObject {
    Q_OBJECT

public:
    explicit AnnotationStore(QObject* parent = nullptr);
    ~AnnotationStore() override;

    static constexpr int kUndoLimit = 200;

    /// Replaces all data and clears undo history. Emits `annotationsReset`
    /// only -- not `annotationsEdited` -- because a load is not a user edit.
    void setAnnotations(SourceAnnotations annotations);
    const SourceAnnotations& annotations() const { return m_annotations; }

    FrameAnnotation frame(int64_t frame) const { return m_annotations.frame(frame); }
    bool hasAnnotations(int64_t frame) const { return m_annotations.hasFrame(frame); }
    QVector<int64_t> annotatedFrames() const { return m_annotations.annotatedFrames(); }

    /// Adds `item` to `frame`, assigning its id. Returns the id, or 0 when the
    /// item is invalid or a per-frame / per-source limit would be exceeded; in
    /// that case nothing changes.
    quint64 addItem(int64_t frame, int64_t mediaTimeUs, AnnotationItem item);

    /// Replaces the item with `item.id` on `frame` (a move or a text edit),
    /// keeping its id and stacking order. One undo step. Returns false for an
    /// unknown id, an invalid item, or no actual change.
    bool replaceItem(int64_t frame, const AnnotationItem& item);

    /// Removes one item. Returns false if the frame or id is unknown.
    bool removeItem(int64_t frame, quint64 itemId);

    /// Removes the topmost (latest) item on `frame` whose id is in `ids`;
    /// used by the eraser, which hit-tests in the UI. Returns the removed id
    /// or 0.
    quint64 removeLastItemAmong(int64_t frame, const QVector<quint64>& ids);

    /// Erases discs of `radius` image pixels centred on each of `centers`
    /// (normalised) from `frame`. Area mode splits freehand strokes and leaves
    /// the pieces outside the discs; whole-stroke mode removes what it touches.
    /// All changes form one edit. Returns true if anything changed.
    bool eraseDiscs(int64_t frame, const QVector<QPointF>& centers, qreal radius,
                    const QSizeF& imageSize, EraserMode mode);

    /// Groups the edits made until endEditGroup() into a single undo step, so
    /// one eraser swipe is undone with one Undo. Groups do not nest beyond a
    /// counter; an empty group records nothing.
    void beginEditGroup();
    void endEditGroup();
    bool inEditGroup() const { return m_groupDepth > 0; }

    /// Removes every drawing on one frame; its written notes stay. Returns
    /// false if it had no drawings.
    bool clearFrame(int64_t frame);

    /// Written notes. Each is one undo step. addNote assigns the id (shared
    /// with drawings, so ids are unique per source) and returns it, or 0 for
    /// an invalid note or a full frame.
    quint64 addNote(int64_t frame, int64_t mediaTimeUs, FrameNote note);
    bool updateNote(int64_t frame, const FrameNote& note);
    bool removeNote(int64_t frame, quint64 noteId);
    /// Removes everything for the source. Returns false if already empty.
    bool clearAll();

    // --- Copy, cut, paste and moving between frames ------------------------

    /// Adds copies of `items` and `notes` on top of what `frame` already has,
    /// each with a fresh id, as one undo step. Returns the new ids (items
    /// first, then notes, in the order given). Returns an empty list and
    /// changes nothing when there is nothing to add, any item or note is
    /// invalid, or a per-frame / per-source limit would be exceeded.
    QVector<quint64> pasteContent(int64_t frame, int64_t mediaTimeUs, const QVector<AnnotationItem>& items,
                                  const QVector<FrameNote>& notes);

    /// Removes the listed drawings and notes from `frame` as one undo step
    /// (a cut). Unknown ids are ignored. Returns false if nothing was removed.
    bool removeContent(int64_t frame, const QVector<quint64>& itemIds, const QVector<quint64>& noteIds);

    /// Moves every drawing and note on `from` onto `to`, on top of what `to`
    /// has, as one undo step. With `copy` the originals stay and the copies
    /// get fresh ids; a move keeps the ids. Returns false and changes nothing
    /// when `from` equals `to`, either is negative, `from` is empty, or a limit
    /// would be exceeded.
    bool moveFrameContent(int64_t from, int64_t to, int64_t toMediaTimeUs, bool copy);

    /// Shifts the drawings and notes on each of `frames` by `offset` frames,
    /// together, as one undo step -- a group of timeline markers dragged as
    /// one. Each frame stays its own frame; frames inside the group may land
    /// on one another's old place, since every source is read before any is
    /// written. Content already on a target frame outside the group is kept,
    /// with the moved content added on top. With `copy` the originals stay and
    /// the copies get fresh ids. `mediaTimeUs` gives the media time for a
    /// target frame that was empty. Returns false and changes nothing when the
    /// offset is zero, a target would be negative, no listed frame has
    /// content, or a limit would be exceeded.
    bool moveFramesContent(const QVector<int64_t>& frames, int64_t offset,
                           const std::function<int64_t(int64_t)>& mediaTimeUs, bool copy);

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    /// Reverts / reapplies the latest edit. Return false when there is nothing.
    bool undo();
    bool redo();
    void clearHistory();

signals:
    /// Emitted after every edit (including undo/redo) once per frame changed.
    void frameChanged(qint64 frame);
    /// Emitted once after an edit, undo or redo has fully applied. The owner
    /// marks the project modified and copies the data back on this.
    void annotationsEdited();
    /// Emitted by setAnnotations().
    void annotationsReset();
    void historyChanged();

private:
    struct FrameChange {
        int64_t frame = 0;
        FrameAnnotation before; // empty items => frame absent
        FrameAnnotation after;
    };
    using Edit = QVector<FrameChange>;

    void commit(Edit edit);
    void apply(const Edit& edit, bool forward);
    quint64 allocateId();
    /// Whether `target` (a frame's current content) can take `itemCount`
    /// more drawings and `noteCount` more notes without passing a limit.
    bool canAccept(const FrameAnnotation& target, qsizetype itemCount, qsizetype noteCount) const;

    SourceAnnotations m_annotations;
    QVector<Edit> m_undo;
    QVector<Edit> m_redo;
    quint64 m_nextId = 1;
    int m_groupDepth = 0;
    Edit m_group; // changes merged by frame while a group is open
};

} // namespace atk::annotation
