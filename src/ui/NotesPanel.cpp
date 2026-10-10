#include "ui/NotesPanel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace atk::ui {
namespace {
constexpr int kFrameRole = Qt::UserRole + 1;
constexpr int kNoteRole = Qt::UserRole + 2;
}

NotesPanel::NotesPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NotesPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto* title = new QLabel(tr("NOTES"), this);
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("Reviewer"), this));
    m_reviewer = new QLineEdit(this);
    m_reviewer->setPlaceholderText(tr("Your name"));
    m_reviewer->setMaxLength(200);
    nameRow->addWidget(m_reviewer, 1);
    layout->addLayout(nameRow);
    connect(m_reviewer, &QLineEdit::editingFinished, this,
            [this] { emit reviewerNameChanged(m_reviewer->text().trimmed()); });

    m_frameLabel = new QLabel(this);
    m_frameLabel->setObjectName(QStringLiteral("NotesFrameLabel"));
    layout->addWidget(m_frameLabel);

    m_composer = new QPlainTextEdit(this);
    m_composer->setObjectName(QStringLiteral("NotesComposer"));
    m_composer->setTabChangesFocus(true);
    m_composer->setFixedHeight(84);
    m_composer->installEventFilter(this);
    layout->addWidget(m_composer);

    auto* buttons = new QHBoxLayout;
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->hide();
    m_submit = new QPushButton(tr("Add Note"), this);
    m_submit->setToolTip(tr("Add the note to the frame on screen (Ctrl+Enter)"));
    m_submit->setDefault(false);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_submit);
    layout->addLayout(buttons);
    connect(m_submit, &QPushButton::clicked, this, &NotesPanel::submit);
    connect(m_cancel, &QPushButton::clicked, this, &NotesPanel::cancelEdit);
    connect(m_composer, &QPlainTextEdit::textChanged, this, &NotesPanel::refreshComposer);

    m_showResolved = new QCheckBox(tr("Show resolved"), this);
    m_showResolved->setChecked(true);
    layout->addWidget(m_showResolved);
    connect(m_showResolved, &QCheckBox::toggled, this, &NotesPanel::rebuildList);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("NotesList"));
    m_list->setWordWrap(true);
    m_list->setSpacing(2);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (const NoteRow* row = rowFor(item)) emit noteActivated(row->frame);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (const NoteRow* row = rowFor(item)) {
            emit noteActivated(row->frame);
            beginEdit(row->frame, row->note.id);
        }
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &NotesPanel::showContextMenu);

    m_emptyHint = new QLabel(tr("No notes yet. Draw on a frame (D) and write what to change here."), this);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setObjectName(QStringLiteral("NotesEmptyHint"));
    layout->addWidget(m_emptyHint);

    // One click to hand the review on, like SyncSketch's export menu.
    auto* exportRow = new QHBoxLayout;
    m_exportPdf = new QPushButton(tr("Export PDF…"), this);
    m_exportPdf->setObjectName(QStringLiteral("NotesExportPdf"));
    m_exportPdf->setToolTip(tr("Save a Review Notes PDF: each drawn or noted frame with its notes"));
    m_exportVideo = new QPushButton(tr("Export Video…"), this);
    m_exportVideo->setObjectName(QStringLiteral("NotesExportVideo"));
    m_exportVideo->setToolTip(tr("Save an MP4 with the drawings and notes burned in on their frames"));
    exportRow->addWidget(m_exportPdf, 1);
    exportRow->addWidget(m_exportVideo, 1);
    layout->addLayout(exportRow);
    connect(m_exportPdf, &QPushButton::clicked, this, &NotesPanel::exportPdfRequested);
    connect(m_exportVideo, &QPushButton::clicked, this, &NotesPanel::exportVideoRequested);

    setCurrentFrame(-1, QString(), false);
    rebuildList();
}

void NotesPanel::setReviewerName(const QString& name) { m_reviewer->setText(name); }
QString NotesPanel::reviewerName() const { return m_reviewer->text().trimmed(); }

void NotesPanel::setCurrentFrame(qint64 frame, const QString& timecode, bool available)
{
    const bool changed = frame != m_currentFrame || available != m_available;
    m_currentFrame = frame;
    m_currentTimecode = timecode;
    m_available = available && frame >= 0;
    if (m_available)
        m_frameLabel->setText(tr("Frame %1  ·  %2").arg(frame + 1).arg(timecode));
    else
        m_frameLabel->setText(tr("Open a clip to add notes"));
    m_exportPdf->setEnabled(m_available);
    m_exportVideo->setEnabled(m_available);
    refreshComposer();
    if (changed) rebuildList(); // highlight the notes on this frame
}

void NotesPanel::setNotes(const QVector<NoteRow>& rows)
{
    m_rows = rows;
    std::stable_sort(m_rows.begin(), m_rows.end(), [](const NoteRow& a, const NoteRow& b) {
        return a.frame != b.frame ? a.frame < b.frame : a.note.createdMsUtc < b.note.createdMsUtc;
    });
    // A note being edited that disappeared (undo, deletion) ends the edit.
    if (m_editNoteId != 0
        && std::none_of(m_rows.cbegin(), m_rows.cend(), [&](const NoteRow& r) { return r.note.id == m_editNoteId; }))
        cancelEdit();
    rebuildList();
}

void NotesPanel::refreshComposer()
{
    const bool editing = m_editNoteId != 0;
    m_composer->setEnabled(m_available || editing);
    m_composer->setPlaceholderText(m_available
        ? tr("Write a note for frame %1...  (Ctrl+Enter to add)").arg(m_currentFrame + 1)
        : tr("Open a clip to add notes"));
    m_submit->setText(editing ? tr("Save") : tr("Add Note"));
    m_cancel->setVisible(editing);
    m_submit->setEnabled((m_available || editing) && !m_composer->toPlainText().trimmed().isEmpty());
}

void NotesPanel::submit()
{
    const QString text = m_composer->toPlainText().trimmed();
    if (text.isEmpty()) return;
    if (m_editNoteId != 0) {
        const auto it = std::find_if(m_rows.cbegin(), m_rows.cend(),
                                     [&](const NoteRow& r) { return r.note.id == m_editNoteId; });
        if (it != m_rows.cend()) {
            annotation::FrameNote note = it->note;
            note.text = text.left(annotation::AnnotationLimits::kMaxNoteLength);
            emit updateNoteRequested(m_editFrame, note);
        }
        cancelEdit();
        return;
    }
    if (!m_available) return;
    emit addNoteRequested(m_currentFrame, text.left(annotation::AnnotationLimits::kMaxNoteLength));
    m_composer->clear();
}

void NotesPanel::beginEdit(qint64 frame, quint64 noteId)
{
    const auto it = std::find_if(m_rows.cbegin(), m_rows.cend(),
                                 [&](const NoteRow& r) { return r.frame == frame && r.note.id == noteId; });
    if (it == m_rows.cend()) return;
    m_editFrame = frame;
    m_editNoteId = noteId;
    m_composer->setPlainText(it->note.text);
    m_composer->moveCursor(QTextCursor::End);
    m_composer->setFocus(Qt::OtherFocusReason);
    refreshComposer();
}

void NotesPanel::cancelEdit()
{
    if (m_editNoteId == 0) return;
    m_editNoteId = 0;
    m_editFrame = -1;
    m_composer->clear();
    refreshComposer();
}

const NoteRow* NotesPanel::rowFor(QListWidgetItem* item) const
{
    if (!item) return nullptr;
    const quint64 id = item->data(kNoteRole).toULongLong();
    for (const NoteRow& row : m_rows)
        if (row.note.id == id) return &row;
    return nullptr;
}

void NotesPanel::rebuildList()
{
    const quint64 selected = m_list->currentItem() ? m_list->currentItem()->data(kNoteRole).toULongLong() : 0;
    m_list->clear();
    int shown = 0;
    for (const NoteRow& row : m_rows) {
        if (row.note.resolved && !m_showResolved->isChecked()) continue;
        QString header = tr("Frame %1  ·  %2").arg(row.frame + 1).arg(row.timecode);
        if (row.frameHasDrawing) header += QStringLiteral("  ✎");
        if (row.note.resolved) header += QStringLiteral("  ✓ ") + tr("resolved");
        const QString author = row.note.author.isEmpty() ? QString() : row.note.author + QStringLiteral(": ");
        auto* item = new QListWidgetItem(header + QLatin1Char('\n') + author + row.note.text, m_list);
        item->setData(kFrameRole, row.frame);
        item->setData(kNoteRole, row.note.id);
        item->setToolTip(tr("Click to go to the frame, double-click to edit, right-click for more"));
        QFont font = item->font();
        if (row.note.resolved) {
            font.setStrikeOut(true);
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
        if (row.frame == m_currentFrame) font.setBold(true); // notes on the frame on screen
        item->setFont(font);
        if (row.note.id == selected) m_list->setCurrentItem(item);
        ++shown;
    }
    m_emptyHint->setVisible(m_rows.isEmpty());
    m_list->setVisible(!m_rows.isEmpty());
    Q_UNUSED(shown);
}

void NotesPanel::showContextMenu(const QPoint& position)
{
    const NoteRow* row = rowFor(m_list->itemAt(position));
    if (!row) return;
    const NoteRow copy = *row; // the list may be rebuilt by the actions below
    QMenu menu(this);
    QAction* go = menu.addAction(tr("Go to Frame"));
    QAction* edit = menu.addAction(tr("Edit"));
    QAction* resolve = menu.addAction(copy.note.resolved ? tr("Reopen") : tr("Mark Resolved"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Delete"));
    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(position));
    if (chosen == go) {
        emit noteActivated(copy.frame);
    } else if (chosen == edit) {
        emit noteActivated(copy.frame);
        beginEdit(copy.frame, copy.note.id);
    } else if (chosen == resolve) {
        annotation::FrameNote note = copy.note;
        note.resolved = !note.resolved;
        emit updateNoteRequested(copy.frame, note);
    } else if (chosen == remove) {
        emit deleteNoteRequested(copy.frame, copy.note.id);
    }
}

bool NotesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (watched == m_composer) {
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && (key->modifiers() & Qt::ControlModifier)) {
                submit();
                return true;
            }
            if (key->key() == Qt::Key_Escape && m_editNoteId != 0) {
                cancelEdit();
                return true;
            }
        } else if (watched == m_list && (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace)) {
            if (const NoteRow* row = rowFor(m_list->currentItem())) {
                emit deleteNoteRequested(row->frame, row->note.id);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace atk::ui
