#pragma once

#include "annotation/AnnotationTypes.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace atk::ui {

/// One note as the panel lists it.
struct NoteRow {
    qint64 frame = 0;        // zero-based source frame
    QString timecode;
    annotation::FrameNote note;
    bool frameHasDrawing = false;
};

/// Right-side review notes, laid out like SyncSketch's notes sidebar: a filter
/// header, the notes as cards, and a composer pinned to the bottom that always
/// writes to the frame on screen.
///
/// The composer belongs to the frame: moving to another frame gives an empty
/// composer, and an unsent draft comes back when you return to its frame.
/// Enter submits, Shift+Enter starts a new line. The panel holds no note data
/// of its own: MainWindow feeds it rows from the AnnotationStore and applies
/// the edits it requests, so undo, saving and export see the same notes.
class NotesPanel : public QWidget {
    Q_OBJECT

public:
    /// Which notes the list shows.
    enum class Filter { All, ThisFrame, Open, Resolved };

    explicit NotesPanel(QWidget* parent = nullptr);

    /// The frame new notes go to. `available` is false with no clip open.
    void setCurrentFrame(qint64 frame, const QString& timecode, bool available);
    qint64 currentFrame() const { return m_currentFrame; }
    void setNotes(const QVector<NoteRow>& rows);
    const QVector<NoteRow>& notes() const { return m_rows; }

    void setReviewerName(const QString& name);
    QString reviewerName() const;

    void setFilter(Filter filter);
    Filter filter() const;

    /// Only notes whose text or author contains `text` (any case); empty
    /// shows all. Combines with the filter.
    void setSearchText(const QString& text);
    QString searchText() const;

    /// Frame order (default), or the most recently written first.
    enum class Sort { FrameOrder, NewestFirst };
    void setSort(Sort sort);
    Sort sort() const { return m_sort; }

    QPlainTextEdit* composer() const { return m_composer; }
    QListWidget* list() const { return m_list; }
    /// Starts editing an existing note in the composer.
    void beginEdit(qint64 frame, quint64 noteId);
    bool isEditing() const { return m_editNoteId != 0; }

signals:
    void addNoteRequested(qint64 frame, const QString& text);
    void updateNoteRequested(qint64 frame, const atk::annotation::FrameNote& note);
    void deleteNoteRequested(qint64 frame, quint64 noteId);
    /// Copy Note / Cut Note from a note's menu: the owner puts the note on the
    /// annotation clipboard (and with `cut`, removes it), for Paste on another frame.
    void copyNoteRequested(qint64 frame, quint64 noteId, bool cut);
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
    /// Moves the current-frame highlight without rebuilding the list, so
    /// scrubbing past many notes stays quick.
    void markCurrentFrame();
    void refreshComposer();
    void setComposerActive(bool active);
    void showContextMenu(const QPoint& position);
    const NoteRow* rowFor(QListWidgetItem* item) const;

    QComboBox* m_filter = nullptr;
    QToolButton* m_searchButton = nullptr;
    QToolButton* m_sortButton = nullptr;
    QLineEdit* m_search = nullptr;
    Sort m_sort = Sort::FrameOrder;
    QLabel* m_count = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_emptyHint = nullptr;
    QFrame* m_composerCard = nullptr;
    QPlainTextEdit* m_composer = nullptr;
    QLabel* m_frameChip = nullptr;
    QPushButton* m_submit = nullptr;
    QPushButton* m_cancel = nullptr;
    QLineEdit* m_reviewer = nullptr;
    QPushButton* m_exportPdf = nullptr;
    QPushButton* m_exportVideo = nullptr;

    QVector<NoteRow> m_rows;
    QHash<qint64, QString> m_drafts; // unsent text per frame
    qint64 m_currentFrame = -1;
    QString m_currentTimecode;
    void refreshSortButton();
    bool m_available = false;
    qint64 m_editFrame = -1;
    quint64 m_editNoteId = 0;
};

} // namespace atk::ui
