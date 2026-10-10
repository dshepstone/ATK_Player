#pragma once

#include "annotation/AnnotationEraser.h"
#include "annotation/AnnotationTypes.h"

#include <QObject>
#include <QVector>

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

    /// Removes every item on one frame. Returns false if it had none.
    bool clearFrame(int64_t frame);
    /// Removes everything for the source. Returns false if already empty.
    bool clearAll();

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

    SourceAnnotations m_annotations;
    QVector<Edit> m_undo;
    QVector<Edit> m_redo;
    quint64 m_nextId = 1;
    int m_groupDepth = 0;
    Edit m_group; // changes merged by frame while a group is open
};

} // namespace atk::annotation
