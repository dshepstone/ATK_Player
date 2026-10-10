#pragma once

#include "annotation/AnnotationTypes.h"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

namespace atk::ui {

/// One note as the panel lists it.
struct NoteRow {
    qint64 frame = 0;        // zero-based source frame
    QString timecode;
    annotation::FrameNote note;
    bool frameHasDrawing = false;
};

/// Right-side review notes, like SyncSketch's notes column.
///
/// The composer always writes to the frame on screen, so a reviewer can draw
/// on a frame and type its note without switching modes. The list shows every
/// note on the clip in frame order; clicking one jumps there. The panel holds
/// no data of its own: MainWindow feeds it rows from the AnnotationStore and
/// applies the edits it requests, so undo, saving and export see the same notes.
class NotesPanel : public QWidget {
    Q_OBJECT

public:
    explicit NotesPanel(QWidget* parent = nullptr);

    /// The frame new notes go to. `available` is false with no clip open.
    void setCurrentFrame(qint64 frame, const QString& timecode, bool available);
    qint64 currentFrame() const { return m_currentFrame; }
    void setNotes(const QVector<NoteRow>& rows);
    const QVector<NoteRow>& notes() const { return m_rows; }

    void setReviewerName(const QString& name);
    QString reviewerName() const;

    QPlainTextEdit* composer() const { return m_composer; }
    QListWidget* list() const { return m_list; }
    /// Starts editing an existing note in the composer.
    void beginEdit(qint64 frame, quint64 noteId);
    bool isEditing() const { return m_editNoteId != 0; }

signals:
    void addNoteRequested(qint64 frame, const QString& text);
    void updateNoteRequested(qint64 frame, const atk::annotation::FrameNote& note);
    void deleteNoteRequested(qint64 frame, quint64 noteId);
    void noteActivated(qint64 frame);
    void reviewerNameChanged(const QString& name);
    /// The panel's export shortcuts: a Review Notes PDF, or the annotated video.
    void exportPdfRequested();
    void exportVideoRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void submit();
    void cancelEdit();
    void rebuildList();
    void refreshComposer();
    void showContextMenu(const QPoint& position);
    const NoteRow* rowFor(QListWidgetItem* item) const;

    QLineEdit* m_reviewer = nullptr;
    QLabel* m_frameLabel = nullptr;
    QPlainTextEdit* m_composer = nullptr;
    QPushButton* m_submit = nullptr;
    QPushButton* m_cancel = nullptr;
    QCheckBox* m_showResolved = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_emptyHint = nullptr;
    QPushButton* m_exportPdf = nullptr;
    QPushButton* m_exportVideo = nullptr;

    QVector<NoteRow> m_rows;
    qint64 m_currentFrame = -1;
    QString m_currentTimecode;
    bool m_available = false;
    qint64 m_editFrame = -1;
    quint64 m_editNoteId = 0;
};

} // namespace atk::ui
