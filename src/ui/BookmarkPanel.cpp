#include "ui/BookmarkPanel.h"

#include "timeline/Bookmark.h"
#include "timeline/TimelineModel.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace atk::ui {

BookmarkPanel::BookmarkPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("BookmarkPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("BookmarkList"));
    layout->addWidget(m_list, 1);
    m_empty = new QLabel(tr("No bookmarks yet.\nPress B to add a point, or create a range below."), this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty);

    m_addPoint = new QPushButton(tr("Add Point Bookmark (B)"), this);
    m_addPoint->setObjectName(QStringLiteral("AddPointBookmark"));
    layout->addWidget(m_addPoint);

    auto* creationGroup = new QGroupBox(tr("Create Range Bookmark"), this);
    auto* creationLayout = new QFormLayout(creationGroup);
    m_createStart = new QSpinBox(creationGroup);
    m_createStart->setObjectName(QStringLiteral("RangeCreationStart"));
    m_createStart->setKeyboardTracking(false);
    m_createEnd = new QSpinBox(creationGroup);
    m_createEnd->setObjectName(QStringLiteral("RangeCreationEnd"));
    m_createEnd->setKeyboardTracking(false);
    creationLayout->addRow(tr("Start"), m_createStart);
    creationLayout->addRow(tr("End"), m_createEnd);
    m_useReviewRange = new QPushButton(tr("Use Current Review Range"), creationGroup);
    m_useReviewRange->setObjectName(QStringLiteral("UseCurrentReviewRange"));
    creationLayout->addRow(m_useReviewRange);
    m_addRange = new QPushButton(tr("Add Range Bookmark"), creationGroup);
    m_addRange->setObjectName(QStringLiteral("AddRangeBookmark"));
    creationLayout->addRow(m_addRange);
    m_createError = new QLabel(creationGroup);
    m_createError->setObjectName(QStringLiteral("RangeCreationError"));
    m_createError->setWordWrap(true);
    m_createError->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    creationLayout->addRow(m_createError);
    layout->addWidget(creationGroup);

    auto* selectedGroup = new QGroupBox(tr("Selected Bookmark"), this);
    auto* selectedLayout = new QVBoxLayout(selectedGroup);

    auto* form = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("BookmarkName"));
    form->addRow(tr("Name"), m_name);
    m_startLabel = new QLabel(tr("Frame"), this);
    m_start = new QSpinBox(this);
    m_start->setObjectName(QStringLiteral("BookmarkStartFrame"));
    m_start->setKeyboardTracking(false);
    form->addRow(m_startLabel, m_start);
    m_endLabel = new QLabel(tr("End"), this);
    m_end = new QSpinBox(this);
    m_end->setObjectName(QStringLiteral("BookmarkEndFrame"));
    m_end->setKeyboardTracking(false);
    form->addRow(m_endLabel, m_end);
    m_color = new QComboBox(this);
    m_color->setObjectName(QStringLiteral("BookmarkColor"));
    m_color->addItem(tr("Default"), timeline::Bookmark::kNoColor);
    const QStringList names{tr("Red"), tr("Orange"), tr("Yellow"), tr("Green"),
                            tr("Cyan"), tr("Blue"), tr("Purple"), tr("Neutral")};
    for (int i = 0; i < timeline::bookmarkColorCount(); ++i) {
        QPixmap swatch(12, 12);
        swatch.fill(timeline::bookmarkColor(i));
        m_color->addItem(QIcon(swatch), names.value(i), i);
    }
    form->addRow(tr("Colour"), m_color);
    m_note = new QPlainTextEdit(this);
    m_note->setObjectName(QStringLiteral("BookmarkNote"));
    m_note->setMaximumHeight(90);
    m_note->setPlaceholderText(tr("Review note"));
    m_note->installEventFilter(this);
    form->addRow(tr("Note"), m_note);
    selectedLayout->addLayout(form);
    m_delete = new QPushButton(tr("Delete Bookmark"), this);
    m_delete->setObjectName(QStringLiteral("DeleteSelectedBookmark"));
    selectedLayout->addWidget(m_delete);
    layout->addWidget(selectedGroup);

    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        m_selectedId = item ? item->data(Qt::UserRole).toULongLong() : 0;
        loadSelection();
        if (!m_refreshing) emit bookmarkSelected(m_selectedId);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit bookmarkActivated(item->data(Qt::UserRole).toULongLong());
    });
    connect(m_name, &QLineEdit::editingFinished, this, &BookmarkPanel::commitMetadata);
    connect(m_color, &QComboBox::currentIndexChanged, this, &BookmarkPanel::commitMetadata);
    connect(m_start, &QSpinBox::editingFinished, this, &BookmarkPanel::commitFrames);
    connect(m_end, &QSpinBox::editingFinished, this, &BookmarkPanel::commitFrames);
    connect(m_delete, &QPushButton::clicked, this, &BookmarkPanel::deleteSelection);
    connect(m_addPoint, &QPushButton::clicked, this, &BookmarkPanel::addPointRequested);
    connect(m_useReviewRange, &QPushButton::clicked, this, &BookmarkPanel::useCurrentReviewRange);
    connect(m_addRange, &QPushButton::clicked, this, &BookmarkPanel::addDraftRange);
    loadSelection();
}

bool BookmarkPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_note && event->type() == QEvent::FocusOut) commitMetadata();
    return QWidget::eventFilter(watched, event);
}

void BookmarkPanel::setModel(timeline::TimelineModel* model)
{
    if (m_model) m_model->disconnect(this);
    m_model = model;
    if (m_model) connect(m_model, &timeline::TimelineModel::bookmarksChanged,
                         this, &BookmarkPanel::rebuildList);
    if (m_model) connect(m_model, &timeline::TimelineModel::frameCountChanged,
                         this, [this] { updateCreationBounds(); });
    updateCreationBounds();
    useCurrentReviewRange();
    rebuildList();
}

void BookmarkPanel::updateCreationBounds()
{
    const int maximum = static_cast<int>(std::max<int64_t>(1, m_model ? m_model->frameCount() : 1));
    m_createStart->setRange(1, maximum);
    m_createEnd->setRange(1, maximum);
}

void BookmarkPanel::useCurrentReviewRange()
{
    if (!m_model) return;
    updateCreationBounds();
    m_createStart->setValue(static_cast<int>(m_model->viewport().startFrame() + 1));
    m_createEnd->setValue(static_cast<int>(m_model->viewport().endFrame() + 1));
    m_createError->clear();
}

void BookmarkPanel::addDraftRange()
{
    const qint64 start = m_createStart->value() - 1;
    const qint64 end = m_createEnd->value() - 1;
    if (start >= end) {
        m_createError->setText(tr("Start must be before End (at least two frames)."));
        return;
    }
    m_createError->clear();
    emit addRangeRequested(start, end);
}

void BookmarkPanel::selectBookmark(quint64 id)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toULongLong() == id) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

void BookmarkPanel::rebuildList()
{
    const quint64 keep = m_selectedId;
    m_refreshing = true;
    m_list->clear();
    if (m_model) for (const timeline::Bookmark& bookmark : m_model->bookmarks()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1    %2").arg(bookmark.displayLabel(), bookmark.frameLabel()), m_list);
        item->setData(Qt::UserRole, bookmark.id);
        item->setToolTip(bookmark.note.isEmpty()
            ? QStringLiteral("%1\n%2").arg(bookmark.displayLabel(), bookmark.frameLabel())
            : QStringLiteral("%1\n%2\n%3").arg(bookmark.displayLabel(), bookmark.frameLabel(), bookmark.note));
        item->setForeground(bookmark.hasColor() ? timeline::bookmarkColor(bookmark.colorIndex) : palette().text().color());
    }
    m_empty->setVisible(m_list->count() == 0);
    m_list->setVisible(m_list->count() != 0);
    m_refreshing = false;
    m_selectedId = 0;
    selectBookmark(keep);
    if (!m_list->currentItem() && m_list->count() > 0) m_list->setCurrentRow(0);
    loadSelection();
}

void BookmarkPanel::loadSelection()
{
    const timeline::Bookmark* bookmark = m_model ? m_model->bookmark(m_selectedId) : nullptr;
    const bool enabled = bookmark != nullptr;
    const QList<QWidget*> editors{m_name, m_note, m_color, m_start, m_end, m_delete};
    for (QWidget* widget : editors)
        widget->setEnabled(enabled);
    if (!bookmark) return;
    m_refreshing = true;
    const int maximum = static_cast<int>(std::max<int64_t>(1, m_model->frameCount()));
    m_start->setRange(1, maximum);
    m_end->setRange(1, maximum);
    m_name->setText(bookmark->name);
    m_note->setPlainText(bookmark->note);
    m_start->setValue(static_cast<int>(bookmark->frame + 1));
    m_end->setValue(static_cast<int>(bookmark->endFrame + 1));
    m_end->setVisible(bookmark->isRange());
    m_endLabel->setVisible(bookmark->isRange());
    m_startLabel->setText(bookmark->isRange() ? tr("Start") : tr("Frame"));
    m_color->setCurrentIndex(m_color->findData(bookmark->colorIndex));
    m_refreshing = false;
}

void BookmarkPanel::commitMetadata()
{
    if (m_refreshing || !m_model) return;
    const timeline::Bookmark* current = m_model->bookmark(m_selectedId);
    if (!current) return;
    timeline::Bookmark edited = *current;
    edited.name = m_name->text().trimmed();
    edited.note = m_note->toPlainText();
    edited.colorIndex = m_color->currentData().toInt();
    m_model->updateBookmark(edited);
}

void BookmarkPanel::commitFrames()
{
    if (m_refreshing || !m_model) return;
    const timeline::Bookmark* current = m_model->bookmark(m_selectedId);
    if (!current) return;
    timeline::Bookmark edited = *current;
    edited.frame = m_start->value() - 1;
    edited.endFrame = edited.isRange() ? m_end->value() - 1 : edited.frame;
    if (edited.frame > edited.endFrame || !m_model->updateBookmark(edited)) loadSelection();
}

void BookmarkPanel::deleteSelection()
{
    if (m_model && m_selectedId) m_model->removeBookmark(m_selectedId);
}

} // namespace atk::ui
