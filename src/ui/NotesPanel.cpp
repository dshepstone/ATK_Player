#include "ui/NotesPanel.h"

#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace atk::ui {
namespace {

constexpr int kFrameRole = Qt::UserRole + 1;
constexpr int kNoteRole = Qt::UserRole + 2;
constexpr int kAuthorRole = Qt::UserRole + 3;
constexpr int kWhenRole = Qt::UserRole + 4;
constexpr int kFlagsRole = Qt::UserRole + 5;

enum CardFlag { HasDrawing = 1, Resolved = 2, OnCurrentFrame = 4 };

/// "Frame 0016": one-based and padded, as SyncSketch labels frames.
QString frameLabel(qint64 zeroBasedFrame)
{
    return NotesPanel::tr("Frame %1").arg(zeroBasedFrame + 1, 4, 10, QLatin1Char('0'));
}

QFont smallerFont(QFont font)
{
    if (font.pixelSize() > 0) font.setPixelSize(std::max(9, font.pixelSize() - 1));
    else font.setPointSizeF(std::max(7.0, font.pointSizeF() * 0.9));
    return font;
}

/// Paints each note as a card: author and time, the note, then a frame chip
/// with drawing and resolved markers. The item's text is the note itself, so
/// lookups and accessibility see plain text.
class NoteCardDelegate final : public QStyledItemDelegate {
public:
    explicit NoteCardDelegate(QListWidget* view) : QStyledItemDelegate(view), m_view(view) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const int width = std::max(120, m_view->viewport()->width() - 2);
        return {width, measure(option.font, index, width).total};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const int flags = index.data(kFlagsRole).toInt();
        const QRect card = option.rect.adjusted(kOuterX, kOuterY, -kOuterX, -kOuterY);
        const Metrics m = measure(option.font, index, option.rect.width());
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (flags & Resolved) p->setOpacity(0.55);

        QColor background = theme::panelBackground();
        if (option.state & QStyle::State_Selected) background = theme::controlPressed();
        else if (option.state & QStyle::State_MouseOver) background = theme::controlBackground();
        QPainterPath path;
        path.addRoundedRect(QRectF(card), 6, 6);
        p->fillPath(path, background);
        if (flags & OnCurrentFrame) {
            // The notes on the frame on screen get an accent edge.
            p->setClipPath(path);
            p->fillRect(QRect(card.left(), card.top(), 3, card.height()), theme::accent());
            p->setClipping(false);
        }

        const QRect inner = card.adjusted(kPadX, kPadY, -kPadX, -kPadY);
        QFont bold = option.font;
        bold.setBold(true);
        const QFont small = smallerFont(option.font);
        const QString author = index.data(kAuthorRole).toString();
        const QString when = index.data(kWhenRole).toString();
        int y = inner.top();
        p->setFont(small);
        p->setPen(theme::textSecondary());
        const int whenWidth = QFontMetrics(small).horizontalAdvance(when);
        p->drawText(QRect(inner.right() - whenWidth, y, whenWidth, m.header), Qt::AlignRight | Qt::AlignVCenter, when);
        p->setFont(bold);
        p->setPen(theme::textPrimary());
        p->drawText(QRect(inner.left(), y, inner.width() - whenWidth - 8, m.header), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(bold).elidedText(author, Qt::ElideRight, inner.width() - whenWidth - 8));
        y += m.header + kGap;

        p->setFont(option.font);
        p->drawText(QRect(inner.left(), y, inner.width(), m.body), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                    index.data(Qt::DisplayRole).toString());
        y += m.body + kGap + 2;

        // Frame chip, then markers.
        p->setFont(small);
        const QFontMetrics sm(small);
        const QString chip = frameLabel(index.data(kFrameRole).toLongLong());
        const QRect chipRect(inner.left(), y, sm.horizontalAdvance(chip) + 12, m.chip);
        QColor chipFill = theme::accent();
        chipFill.setAlpha(40);
        p->setPen(Qt::NoPen);
        p->setBrush(chipFill);
        p->drawRoundedRect(chipRect, 4, 4);
        p->setPen(theme::accent());
        p->drawText(chipRect, Qt::AlignCenter, chip);
        int x = chipRect.right() + 8;
        auto marker = [&](const QString& text, const QColor& color) {
            p->setPen(color);
            const int w = sm.horizontalAdvance(text);
            p->drawText(QRect(x, y, w, m.chip), Qt::AlignVCenter | Qt::AlignLeft, text);
            x += w + 10;
        };
        if (flags & HasDrawing) marker(NotesPanel::tr("✎ Drawing"), theme::textSecondary());
        if (flags & Resolved) marker(NotesPanel::tr("✓ Resolved"), QColor(0x5F, 0xB9, 0x7A));
        p->restore();
    }

private:
    static constexpr int kOuterX = 6, kOuterY = 3, kPadX = 10, kPadY = 8, kGap = 4;

    struct Metrics {
        int header = 0, body = 0, chip = 0, total = 0;
    };

    Metrics measure(const QFont& font, const QModelIndex& index, int rowWidth) const
    {
        Metrics m;
        QFont bold = font;
        bold.setBold(true);
        const int textWidth = std::max(40, rowWidth - 2 * kOuterX - 2 * kPadX);
        m.header = QFontMetrics(bold).height();
        m.body = QFontMetrics(font)
                     .boundingRect(QRect(0, 0, textWidth, 100000), Qt::TextWordWrap | Qt::AlignLeft,
                                   index.data(Qt::DisplayRole).toString())
                     .height();
        m.chip = QFontMetrics(smallerFont(font)).height() + 4;
        m.total = 2 * kOuterY + 2 * kPadY + m.header + kGap + m.body + kGap + 2 + m.chip;
        return m;
    }

    QListWidget* m_view;
};

QString panelStyleSheet()
{
    const auto c = [](const QColor& color) { return color.name(QColor::HexRgb); };
    return QStringLiteral(R"CSS(
#NotesHeader, #NotesFooter { background-color: %1; }
#NotesHeader { border-bottom: 1px solid %2; }
#NotesFooter { border-top: 1px solid %2; }
#NotesHeader QLabel, #NotesFooter QLabel { background: transparent; }
QLabel#NotesCount {
    background-color: %3; color: %4; border-radius: 8px; padding: 1px 7px; font-size: 11px;
}
QComboBox#NotesFilter { background: transparent; border: none; font-size: 13px; padding: 2px 4px; }
QComboBox#NotesFilter:hover { background-color: %3; border-radius: 4px; }
QListWidget#NotesList { background-color: %5; border: none; }
QWidget#NotesEmptyPage { background-color: %5; }
QLabel#NotesEmptyHint { color: %4; font-size: 13px; background: transparent; }
QFrame#NotesComposerCard { background-color: %5; border: 1px solid %6; border-radius: 6px; }
QFrame#NotesComposerCard[active="true"] { border: 1px solid %7; }
QFrame#NotesComposerCard QWidget { background: transparent; }
QPlainTextEdit#NotesComposer { border: none; padding: 0; font-size: 13px; }
QLabel#NotesFrameChip { color: %7; font-weight: 600; }
QPushButton#NotesSubmit { padding: 4px 14px; border-radius: 4px; }
QPushButton#NotesSubmit:enabled { background-color: %7; color: #ffffff; border: none; }
QLineEdit#NotesReviewer { background: transparent; border: none; border-bottom: 1px solid %6; padding: 1px 2px; }
QLabel#NotesReviewerLabel { color: %4; }
)CSS")
        .arg(c(theme::panelBackground()), c(theme::panelBorder()), c(theme::controlBackground()),
             c(theme::textSecondary()), c(theme::windowBackground()), c(theme::controlHover()), c(theme::accent()));
}

} // namespace

NotesPanel::NotesPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NotesPanel"));
    setStyleSheet(panelStyleSheet());
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header: which notes, and how many.
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("NotesHeader"));
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(8, 6, 10, 6);
    m_filter = new QComboBox(header);
    m_filter->setObjectName(QStringLiteral("NotesFilter"));
    m_filter->addItem(tr("All Notes"), int(Filter::All));
    m_filter->addItem(tr("This Frame"), int(Filter::ThisFrame));
    m_filter->addItem(tr("Open"), int(Filter::Open));
    m_filter->addItem(tr("Resolved"), int(Filter::Resolved));
    m_filter->setToolTip(tr("Show all notes, only this frame's, or only open or resolved notes"));
    m_count = new QLabel(QStringLiteral("0"), header);
    m_count->setObjectName(QStringLiteral("NotesCount"));
    m_count->setToolTip(tr("Notes on this clip"));
    headerRow->addWidget(m_filter);
    headerRow->addWidget(m_count);
    headerRow->addStretch(1);
    // Search and sort, as in SyncSketch's notes header.
    auto headerButton = [header](const QString& icon, const QString& tip, const QString& name) {
        auto* button = new QToolButton(header);
        button->setObjectName(name);
        button->setIcon(icons::icon(icon));
        button->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setProperty("atkRole", "iconButton");
        return button;
    };
    m_searchButton = headerButton(QStringLiteral("search"), tr("Search notes"), QStringLiteral("NotesSearchButton"));
    m_searchButton->setCheckable(true);
    m_sortButton = headerButton(QStringLiteral("arrow-up-down"), QString(), QStringLiteral("NotesSortButton"));
    headerRow->addWidget(m_searchButton);
    headerRow->addWidget(m_sortButton);
    root->addWidget(header);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &NotesPanel::rebuildList);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("NotesSearch"));
    m_search->setAccessibleName(tr("Search notes"));
    m_search->setPlaceholderText(tr("Search notes and authors"));
    m_search->setClearButtonEnabled(true);
    m_search->setVisible(false);
    auto* searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(8, 0, 8, 6);
    searchRow->addWidget(m_search);
    root->addLayout(searchRow);
    connect(m_search, &QLineEdit::textChanged, this, &NotesPanel::rebuildList);
    connect(m_searchButton, &QToolButton::toggled, this, [this](bool on) {
        m_search->setVisible(on);
        if (on) m_search->setFocus(Qt::ShortcutFocusReason);
        else m_search->clear(); // a hidden search must not hide notes
    });
    connect(m_sortButton, &QToolButton::clicked, this, [this] {
        setSort(m_sort == Sort::FrameOrder ? Sort::NewestFirst : Sort::FrameOrder);
    });
    refreshSortButton();

    // The notes, or a hint when there are none to show.
    m_stack = new QStackedWidget(this);
    m_list = new QListWidget(m_stack);
    m_list->setObjectName(QStringLiteral("NotesList"));
    m_list->setAccessibleName(tr("Notes"));
    m_list->setItemDelegate(new NoteCardDelegate(m_list));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMouseTracking(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    m_stack->addWidget(m_list);
    auto* emptyPage = new QWidget(m_stack);
    emptyPage->setObjectName(QStringLiteral("NotesEmptyPage"));
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    m_emptyHint = new QLabel(emptyPage);
    m_emptyHint->setObjectName(QStringLiteral("NotesEmptyHint"));
    m_emptyHint->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_emptyHint->setWordWrap(true);
    emptyLayout->setContentsMargins(16, 28, 16, 16);
    emptyLayout->addWidget(m_emptyHint);
    emptyLayout->addStretch(1);
    m_stack->addWidget(emptyPage);
    root->addWidget(m_stack, 1);
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

    // Footer: the composer for the frame on screen, who is writing, and export.
    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("NotesFooter"));
    auto* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(10, 10, 10, 10);
    footerLayout->setSpacing(8);

    m_composerCard = new QFrame(footer);
    m_composerCard->setObjectName(QStringLiteral("NotesComposerCard"));
    auto* cardLayout = new QVBoxLayout(m_composerCard);
    cardLayout->setContentsMargins(8, 6, 8, 6);
    cardLayout->setSpacing(4);
    m_composer = new QPlainTextEdit(m_composerCard);
    m_composer->setObjectName(QStringLiteral("NotesComposer"));
    m_composer->setTabChangesFocus(true);
    m_composer->setFrameShape(QFrame::NoFrame);
    m_composer->setFixedHeight(68);
    m_composer->installEventFilter(this);
    cardLayout->addWidget(m_composer);
    auto* cardRow = new QHBoxLayout;
    m_frameChip = new QLabel(m_composerCard);
    m_frameChip->setObjectName(QStringLiteral("NotesFrameChip"));
    m_cancel = new QPushButton(tr("Cancel"), m_composerCard);
    m_cancel->setObjectName(QStringLiteral("NotesCancel"));
    m_cancel->hide();
    m_submit = new QPushButton(tr("Submit"), m_composerCard);
    m_submit->setObjectName(QStringLiteral("NotesSubmit"));
    m_submit->setToolTip(tr("Add the note to the frame on screen (Enter; Shift+Enter for a new line)"));
    cardRow->addWidget(m_frameChip);
    cardRow->addStretch(1);
    cardRow->addWidget(m_cancel);
    cardRow->addWidget(m_submit);
    cardLayout->addLayout(cardRow);
    footerLayout->addWidget(m_composerCard);
    connect(m_submit, &QPushButton::clicked, this, &NotesPanel::submit);
    connect(m_cancel, &QPushButton::clicked, this, &NotesPanel::cancelEdit);
    connect(m_composer, &QPlainTextEdit::textChanged, this, &NotesPanel::refreshComposer);

    auto* nameRow = new QHBoxLayout;
    auto* nameLabel = new QLabel(tr("Posting as"), footer);
    nameLabel->setObjectName(QStringLiteral("NotesReviewerLabel"));
    m_reviewer = new QLineEdit(footer);
    m_reviewer->setObjectName(QStringLiteral("NotesReviewer"));
    m_reviewer->setAccessibleName(tr("Reviewer name"));
    m_reviewer->setPlaceholderText(tr("Your name"));
    m_reviewer->setMaxLength(200);
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(m_reviewer, 1);
    footerLayout->addLayout(nameRow);
    connect(m_reviewer, &QLineEdit::editingFinished, this,
            [this] { emit reviewerNameChanged(m_reviewer->text().trimmed()); });

    auto* exportRow = new QHBoxLayout;
    m_exportPdf = new QPushButton(tr("Export PDF…"), footer);
    m_exportPdf->setObjectName(QStringLiteral("NotesExportPdf"));
    m_exportPdf->setToolTip(tr("Save a Review Notes PDF: each drawn or noted frame with its notes"));
    m_exportVideo = new QPushButton(tr("Export Video…"), footer);
    m_exportVideo->setObjectName(QStringLiteral("NotesExportVideo"));
    m_exportVideo->setToolTip(tr("Save an MP4 with the drawings and notes burned in on their frames"));
    exportRow->addWidget(m_exportPdf, 1);
    exportRow->addWidget(m_exportVideo, 1);
    footerLayout->addLayout(exportRow);
    connect(m_exportPdf, &QPushButton::clicked, this, &NotesPanel::exportPdfRequested);
    connect(m_exportVideo, &QPushButton::clicked, this, &NotesPanel::exportVideoRequested);
    root->addWidget(footer);

    setMinimumWidth(240);
    setCurrentFrame(-1, QString(), false);
    rebuildList();
}

void NotesPanel::setReviewerName(const QString& name) { m_reviewer->setText(name); }
QString NotesPanel::reviewerName() const { return m_reviewer->text().trimmed(); }

void NotesPanel::setFilter(Filter filter)
{
    const int index = m_filter->findData(int(filter));
    if (index >= 0) m_filter->setCurrentIndex(index);
}

NotesPanel::Filter NotesPanel::filter() const { return Filter(m_filter->currentData().toInt()); }

void NotesPanel::setCurrentFrame(qint64 frame, const QString& timecode, bool available)
{
    const bool frameChanged = frame != m_currentFrame;
    const bool changed = frameChanged || (available && frame >= 0) != m_available;
    if (frameChanged) {
        if (m_editNoteId != 0) {
            // Leaving the frame of the note being edited ends the edit.
            if (frame != m_editFrame) cancelEdit();
        } else if (m_currentFrame >= 0) {
            // Keep an unsent draft with its frame.
            const QString draft = m_composer->toPlainText();
            if (draft.trimmed().isEmpty()) m_drafts.remove(m_currentFrame);
            else m_drafts.insert(m_currentFrame, draft);
        }
    }
    m_currentFrame = frame;
    m_currentTimecode = timecode;
    m_available = available && frame >= 0;
    if (frameChanged && m_editNoteId == 0) {
        m_composer->setPlainText(m_drafts.value(frame));
        m_composer->moveCursor(QTextCursor::End);
    }
    m_exportPdf->setEnabled(m_available);
    m_exportVideo->setEnabled(m_available);
    refreshComposer();
    if (!changed) return;
    // Only "this frame" shows different notes on another frame; otherwise the
    // same rows stay and only the highlight moves.
    if (frameChanged && filter() != Filter::ThisFrame && m_list->count() > 0) markCurrentFrame();
    else rebuildList();
}

void NotesPanel::markCurrentFrame()
{
    QListWidgetItem* firstOnFrame = nullptr;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        const bool onFrame = item->data(kFrameRole).toLongLong() == m_currentFrame;
        const int flags = item->data(kFlagsRole).toInt();
        const int marked = onFrame ? (flags | OnCurrentFrame) : (flags & ~OnCurrentFrame);
        if (marked != flags) item->setData(kFlagsRole, marked);
        if (onFrame && !firstOnFrame) firstOnFrame = item;
    }
    if (firstOnFrame) m_list->scrollToItem(firstOnFrame, QAbstractItemView::EnsureVisible);
}

void NotesPanel::setSearchText(const QString& text)
{
    if (!text.isEmpty()) m_searchButton->setChecked(true);
    m_search->setText(text);
}

QString NotesPanel::searchText() const
{
    return m_search->text().trimmed();
}

void NotesPanel::setSort(Sort sort)
{
    if (m_sort == sort) return;
    m_sort = sort;
    refreshSortButton();
    rebuildList();
}

void NotesPanel::refreshSortButton()
{
    const QString tip = m_sort == Sort::FrameOrder ? tr("Sorted by frame. Click for newest first.")
                                                   : tr("Newest first. Click to sort by frame.");
    m_sortButton->setToolTip(tip);
    m_sortButton->setAccessibleName(tip);
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
    m_composer->setPlaceholderText(m_available ? tr("Add a note for this frame…") : tr("Open a clip to add notes"));
    if (editing)
        m_frameChip->setText(tr("Editing · %1").arg(frameLabel(m_editFrame)));
    else if (m_available)
        m_frameChip->setText(QStringLiteral("%1  ·  %2").arg(frameLabel(m_currentFrame), m_currentTimecode));
    else
        m_frameChip->clear();
    m_submit->setText(editing ? tr("Save") : tr("Submit"));
    m_cancel->setVisible(editing);
    m_submit->setEnabled((m_available || editing) && !m_composer->toPlainText().trimmed().isEmpty());
}

void NotesPanel::setComposerActive(bool active)
{
    if (m_composerCard->property("active").toBool() == active) return;
    m_composerCard->setProperty("active", active);
    m_composerCard->style()->unpolish(m_composerCard);
    m_composerCard->style()->polish(m_composerCard);
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
    const qint64 frame = m_currentFrame;
    emit addNoteRequested(frame, text.left(annotation::AnnotationLimits::kMaxNoteLength));
    m_drafts.remove(frame);
    m_composer->clear();
}

void NotesPanel::beginEdit(qint64 frame, quint64 noteId)
{
    const auto it = std::find_if(m_rows.cbegin(), m_rows.cend(),
                                 [&](const NoteRow& r) { return r.frame == frame && r.note.id == noteId; });
    if (it == m_rows.cend()) return;
    // Park any draft for the frame on screen before the composer is reused.
    if (m_editNoteId == 0 && m_currentFrame >= 0 && !m_composer->toPlainText().trimmed().isEmpty())
        m_drafts.insert(m_currentFrame, m_composer->toPlainText());
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
    m_composer->setPlainText(m_drafts.value(m_currentFrame));
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
    const int scroll = m_list->verticalScrollBar()->value();
    const Filter mode = filter();
    const QString search = searchText();
    m_list->clear();
    QListWidgetItem* firstOnFrame = nullptr;
    const QLocale locale;
    QVector<const NoteRow*> shown;
    shown.reserve(m_rows.size());
    for (const NoteRow& row : m_rows) shown.append(&row);
    if (m_sort == Sort::NewestFirst)
        std::stable_sort(shown.begin(), shown.end(), [](const NoteRow* a, const NoteRow* b) {
            return a->note.createdMsUtc > b->note.createdMsUtc;
        });
    for (const NoteRow* rowPointer : shown) {
        const NoteRow& row = *rowPointer;
        if (mode == Filter::ThisFrame && row.frame != m_currentFrame) continue;
        if (mode == Filter::Open && row.note.resolved) continue;
        if (mode == Filter::Resolved && !row.note.resolved) continue;
        if (!search.isEmpty() && !row.note.text.contains(search, Qt::CaseInsensitive)
            && !row.note.author.contains(search, Qt::CaseInsensitive))
            continue;
        auto* item = new QListWidgetItem(row.note.text, m_list);
        item->setData(kFrameRole, row.frame);
        item->setData(kNoteRole, row.note.id);
        item->setData(kAuthorRole, row.note.author.isEmpty() ? tr("Anonymous") : row.note.author);
        item->setData(kWhenRole, row.note.createdMsUtc > 0
            ? locale.toString(QDateTime::fromMSecsSinceEpoch(row.note.createdMsUtc).toLocalTime(),
                            QStringLiteral("MMM d, HH:mm"))
            : QString());
        int flags = 0;
        if (row.frameHasDrawing) flags |= HasDrawing;
        if (row.note.resolved) flags |= Resolved;
        if (row.frame == m_currentFrame) flags |= OnCurrentFrame;
        item->setData(kFlagsRole, flags);
        item->setToolTip(tr("%1  ·  %2\nClick to go to the frame, double-click to edit, right-click for more")
                             .arg(frameLabel(row.frame), row.timecode));
        if (row.note.id == selected) m_list->setCurrentItem(item);
        if (!firstOnFrame && row.frame == m_currentFrame) firstOnFrame = item;
    }
    m_count->setText(QString::number(m_rows.size()));

    if (m_list->count() > 0) {
        m_stack->setCurrentIndex(0);
        m_list->verticalScrollBar()->setValue(scroll);
        if (firstOnFrame) m_list->scrollToItem(firstOnFrame, QAbstractItemView::EnsureVisible);
        return;
    }
    QString hint;
    if (!m_available && m_rows.isEmpty()) hint = tr("Open a clip to start reviewing.");
    else if (m_rows.isEmpty()) hint = tr("No notes yet!\nLeave the first one below…");
    else if (!search.isEmpty()) hint = tr("No notes match \"%1\".").arg(search);
    else if (mode == Filter::ThisFrame) hint = tr("No notes on %1 yet.\nWrite one below…").arg(frameLabel(m_currentFrame));
    else if (mode == Filter::Open) hint = tr("No open notes. Everything is resolved.");
    else hint = tr("No resolved notes yet.");
    m_emptyHint->setText(hint);
    m_stack->setCurrentIndex(1);
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
    QAction* copyNote = menu.addAction(tr("Copy Note"));
    QAction* cutNote = menu.addAction(tr("Cut Note"));
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
    } else if (chosen == copyNote || chosen == cutNote) {
        emit copyNoteRequested(copy.frame, copy.note.id, chosen == cutNote);
    } else if (chosen == remove) {
        emit deleteNoteRequested(copy.frame, copy.note.id);
    }
}

bool NotesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_composer && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        setComposerActive(event->type() == QEvent::FocusIn);
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (watched == m_composer) {
            // Enter submits; Shift+Enter starts a new line (as in SyncSketch).
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && !(key->modifiers() & Qt::ShiftModifier)) {
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
