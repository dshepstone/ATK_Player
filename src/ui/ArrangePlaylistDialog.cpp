#include "ui/ArrangePlaylistDialog.h"

#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace atk::ui {
namespace {

enum Column { OrderColumn, NameColumn, CategoryColumn, DateColumn, LengthColumn };
constexpr int kIdRole = Qt::UserRole;

QString lengthText(qint64 durationUs)
{
    if (durationUs < 0) return {};
    const qint64 total = durationUs / 1'000'000;
    const qint64 hours = total / 3600, minutes = (total / 60) % 60, seconds = total % 60;
    return hours > 0 ? QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

/// Natural, case-blind order: runs of digits compare as numbers, so "shot2"
/// sorts before "shot10". Written out rather than left to QCollator, whose
/// numeric mode some platforms' collation backends ignore.
int naturalCompare(const QString& a, const QString& b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            qsizetype endA = i, endB = j;
            while (endA < a.size() && a.at(endA).isDigit()) ++endA;
            while (endB < b.size() && b.at(endB).isDigit()) ++endB;
            // Compare digit runs by value: drop leading zeros, then length, then digits.
            qsizetype startA = i, startB = j;
            while (startA + 1 < endA && a.at(startA) == QLatin1Char('0')) ++startA;
            while (startB + 1 < endB && b.at(startB) == QLatin1Char('0')) ++startB;
            if (endA - startA != endB - startB) return endA - startA < endB - startB ? -1 : 1;
            if (const int digits = QStringView(a).mid(startA, endA - startA)
                                       .compare(QStringView(b).mid(startB, endB - startB));
                digits != 0)
                return digits;
            i = endA;
            j = endB;
            continue;
        }
        if (const int c = QString(a.at(i)).compare(QString(b.at(j)), Qt::CaseInsensitive); c != 0) return c;
        ++i;
        ++j;
    }
    if (i < a.size() || j < b.size()) return i < a.size() ? 1 : -1;
    return a.compare(b, Qt::CaseInsensitive);
}

int statusRank(project::ClipStatus status)
{
    // Work still to do first: not reviewed, in review, needs changes, approved.
    switch (status) {
    case project::ClipStatus::NotReviewed: return 0;
    case project::ClipStatus::InReview: return 1;
    case project::ClipStatus::NeedsChanges: return 2;
    case project::ClipStatus::Approved: return 3;
    }
    return 0;
}

} // namespace

void ArrangePlaylistDialog::sortClips(QVector<Clip>& clips, SortKey key, const QStringList& categories)
{
    if (key == SortKey::Reverse) {
        std::reverse(clips.begin(), clips.end());
        return;
    }
    const auto categoryRank = [&categories](const Clip& clip) {
        const qsizetype index = clip.category.isEmpty() ? -1 : categories.indexOf(clip.category);
        return index < 0 ? categories.size() : index; // uncategorized last
    };
    // Unknown dates and lengths go last whichever way the list is sorted.
    const auto less = [&](const Clip& a, const Clip& b) {
        switch (key) {
        case SortKey::NameAscending: return naturalCompare(a.name, b.name) < 0;
        case SortKey::NameDescending: return naturalCompare(a.name, b.name) > 0;
        case SortKey::DateOldestFirst:
        case SortKey::DateNewestFirst:
            if (a.modified.isValid() != b.modified.isValid()) return a.modified.isValid();
            return key == SortKey::DateOldestFirst ? a.modified < b.modified : a.modified > b.modified;
        case SortKey::LengthShortestFirst:
        case SortKey::LengthLongestFirst:
            if ((a.durationUs >= 0) != (b.durationUs >= 0)) return a.durationUs >= 0;
            return key == SortKey::LengthShortestFirst ? a.durationUs < b.durationUs : a.durationUs > b.durationUs;
        case SortKey::Category: return categoryRank(a) < categoryRank(b);
        case SortKey::Status: return statusRank(a.status) < statusRank(b.status);
        case SortKey::Reverse: return false;
        }
        return false;
    };
    std::stable_sort(clips.begin(), clips.end(), less);
}

ArrangePlaylistDialog::ArrangePlaylistDialog(QVector<Clip> clips, QStringList categories, QWidget* parent)
    : QDialog(parent), m_categories(std::move(categories))
{
    setObjectName(QStringLiteral("ArrangePlaylistDialog"));
    setWindowTitle(tr("Arrange Playlist"));
    resize(760, 560);
    auto* root = new QVBoxLayout(this);

    auto* hint = new QLabel(tr("Select clips (Shift or Ctrl to pick several), then move them with the arrows or drag "
                               "them. Sort reorders the whole playlist. Nothing changes until you apply."), this);
    hint->setWordWrap(true);
    hint->setProperty("atkRole", "placeholder");
    root->addWidget(hint);

    // Sort menu.
    auto* sortRow = new QHBoxLayout;
    auto* sort = new QToolButton(this);
    sort->setObjectName(QStringLiteral("ArrangeSortButton"));
    sort->setText(tr("Sort by"));
    sort->setIcon(icons::icon(QStringLiteral("arrow-up-down")));
    sort->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sort->setPopupMode(QToolButton::InstantPopup);
    auto* sortMenu = new QMenu(sort);
    const auto addSort = [this, sortMenu](const QString& text, SortKey key) {
        sortMenu->addAction(text, this, [this, key] { sortBy(key); });
    };
    addSort(tr("Name (A to Z)"), SortKey::NameAscending);
    addSort(tr("Name (Z to A)"), SortKey::NameDescending);
    sortMenu->addSeparator();
    addSort(tr("File date (oldest first)"), SortKey::DateOldestFirst);
    addSort(tr("File date (newest first)"), SortKey::DateNewestFirst);
    sortMenu->addSeparator();
    addSort(tr("Length (shortest first)"), SortKey::LengthShortestFirst);
    addSort(tr("Length (longest first)"), SortKey::LengthLongestFirst);
    sortMenu->addSeparator();
    addSort(tr("Category (grouped, in category order)"), SortKey::Category);
    addSort(tr("Review status (still to do first)"), SortKey::Status);
    sortMenu->addSeparator();
    addSort(tr("Reverse order"), SortKey::Reverse);
    sort->setMenu(sortMenu);
    sortRow->addWidget(sort);
    sortRow->addStretch(1);
    root->addLayout(sortRow);

    // The list, with the move buttons beside it.
    auto* middle = new QHBoxLayout;
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("ArrangePlaylistList"));
    m_list->setAccessibleName(tr("Clips in playlist order"));
    m_list->setHeaderLabels({tr("#"), tr("Clip"), tr("Category"), tr("File date"), tr("Length")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    for (int column : {OrderColumn, CategoryColumn, DateColumn, LengthColumn})
        m_list->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    middle->addWidget(m_list, 1);

    auto* moves = new QVBoxLayout;
    const auto addMove = [this, moves](const QString& icon, const QString& tip, int delta, const QString& name) {
        auto* button = new QPushButton(icons::icon(icon), QString(), this);
        button->setObjectName(name);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setIconSize(QSize(theme::metrics::iconSmall, theme::metrics::iconSmall));
        connect(button, &QPushButton::clicked, this, [this, delta] { moveSelected(delta); });
        moves->addWidget(button);
        m_moveButtons.append(button);
    };
    addMove(QStringLiteral("arrow-up-to-line"), tr("Move to the top"), -1'000'000, QStringLiteral("ArrangeMoveTop"));
    addMove(QStringLiteral("arrow-up"), tr("Move up"), -1, QStringLiteral("ArrangeMoveUp"));
    addMove(QStringLiteral("arrow-down"), tr("Move down"), 1, QStringLiteral("ArrangeMoveDown"));
    addMove(QStringLiteral("arrow-down-to-line"), tr("Move to the bottom"), 1'000'000, QStringLiteral("ArrangeMoveBottom"));
    moves->addStretch(1);
    middle->addLayout(moves);
    root->addLayout(middle, 1);

    // Categories: pick one, file the selected clips under it; make, rename or
    // remove categories.
    auto* categoryRow = new QHBoxLayout;
    auto* categoryIcon = new QLabel(this);
    categoryIcon->setPixmap(icons::pixmap(QStringLiteral("tag"), QSize(16, 16), theme::textSecondary(),
                                          devicePixelRatioF()));
    categoryRow->addWidget(categoryIcon);
    categoryRow->addWidget(new QLabel(tr("Category:"), this));
    m_category = new QComboBox(this);
    m_category->setObjectName(QStringLiteral("ArrangeCategory"));
    m_category->setMinimumWidth(180);
    categoryRow->addWidget(m_category);
    m_setCategory = new QPushButton(tr("Set for Selected"), this);
    m_setCategory->setObjectName(QStringLiteral("ArrangeSetCategory"));
    m_setCategory->setToolTip(tr("File the selected clips under this category"));
    categoryRow->addWidget(m_setCategory);
    categoryRow->addSpacing(12);
    auto* newCategory = new QPushButton(tr("New…"), this);
    newCategory->setObjectName(QStringLiteral("ArrangeNewCategory"));
    newCategory->setToolTip(tr("Make a category, such as a class section, a shot type or a priority"));
    categoryRow->addWidget(newCategory);
    m_renameCategory = new QPushButton(tr("Rename…"), this);
    m_renameCategory->setObjectName(QStringLiteral("ArrangeRenameCategory"));
    categoryRow->addWidget(m_renameCategory);
    m_removeCategory = new QPushButton(tr("Remove"), this);
    m_removeCategory->setObjectName(QStringLiteral("ArrangeRemoveCategory"));
    m_removeCategory->setToolTip(tr("Remove this category; its clips keep their place with no category"));
    categoryRow->addWidget(m_removeCategory);
    categoryRow->addStretch(1);
    root->addLayout(categoryRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("ArrangeApply"));
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_setCategory, &QPushButton::clicked, this,
            [this] { setSelectedCategory(m_category->currentData().toString()); });
    connect(newCategory, &QPushButton::clicked, this, &ArrangePlaylistDialog::addCategory);
    connect(m_renameCategory, &QPushButton::clicked, this, &ArrangePlaylistDialog::renameCategory);
    connect(m_removeCategory, &QPushButton::clicked, this, &ArrangePlaylistDialog::removeCategory);
    connect(m_category, &QComboBox::currentIndexChanged, this, &ArrangePlaylistDialog::refreshButtons);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &ArrangePlaylistDialog::refreshButtons);
    // A drag moves rows through the model; number them again once it lands.
    const auto renumberLater = [this] { QTimer::singleShot(0, this, &ArrangePlaylistDialog::renumber); };
    connect(m_list->model(), &QAbstractItemModel::rowsInserted, this, renumberLater);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, renumberLater);

    fill(clips);
    refreshCategoryChoices();
    refreshButtons();
}

void ArrangePlaylistDialog::fill(const QVector<Clip>& clips)
{
    // Selection survives a re-fill, by id.
    QSet<QUuid> selected;
    for (QTreeWidgetItem* item : m_list->selectedItems()) selected.insert(item->data(OrderColumn, kIdRole).toUuid());
    m_list->clear();
    const QLocale locale;
    for (const Clip& clip : clips) {
        auto* item = new QTreeWidgetItem(m_list);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
        item->setData(OrderColumn, kIdRole, clip.id);
        item->setText(NameColumn, clip.name);
        item->setToolTip(NameColumn, clip.name);
        item->setText(CategoryColumn, clip.category);
        item->setData(DateColumn, kIdRole, clip.modified);
        item->setText(DateColumn, clip.modified.isValid() ? locale.toString(clip.modified, QLocale::ShortFormat)
                                                          : QString());
        item->setData(LengthColumn, kIdRole, clip.durationUs);
        item->setText(LengthColumn, lengthText(clip.durationUs));
        item->setTextAlignment(LengthColumn, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(NameColumn, kIdRole, int(clip.status));
        if (selected.contains(clip.id)) item->setSelected(true);
    }
    renumber();
}

void ArrangePlaylistDialog::renumber()
{
    for (int row = 0; row < m_list->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = m_list->topLevelItem(row);
        item->setText(OrderColumn, QString::number(row + 1));
        item->setTextAlignment(OrderColumn, Qt::AlignRight | Qt::AlignVCenter);
    }
}

QVector<ArrangePlaylistDialog::Clip> ArrangePlaylistDialog::clips() const
{
    QVector<Clip> result;
    result.reserve(m_list->topLevelItemCount());
    for (int row = 0; row < m_list->topLevelItemCount(); ++row) {
        const QTreeWidgetItem* item = m_list->topLevelItem(row);
        Clip clip;
        clip.id = item->data(OrderColumn, kIdRole).toUuid();
        clip.name = item->text(NameColumn);
        clip.category = item->text(CategoryColumn);
        clip.modified = item->data(DateColumn, kIdRole).toDateTime();
        clip.durationUs = item->data(LengthColumn, kIdRole).toLongLong();
        clip.status = project::ClipStatus(item->data(NameColumn, kIdRole).toInt());
        result.append(clip);
    }
    return result;
}

QVector<QUuid> ArrangePlaylistDialog::order() const
{
    QVector<QUuid> ids;
    for (const Clip& clip : clips()) ids.append(clip.id);
    return ids;
}

void ArrangePlaylistDialog::sortBy(SortKey key)
{
    QVector<Clip> sorted = clips();
    sortClips(sorted, key, m_categories);
    fill(sorted);
}

void ArrangePlaylistDialog::selectRows(const QVector<int>& rows)
{
    m_list->clearSelection();
    for (int row : rows)
        if (QTreeWidgetItem* item = m_list->topLevelItem(row)) item->setSelected(true);
    if (!rows.isEmpty())
        if (QTreeWidgetItem* item = m_list->topLevelItem(rows.first())) m_list->scrollToItem(item);
}

void ArrangePlaylistDialog::moveSelected(int delta)
{
    QVector<Clip> all = clips();
    QVector<bool> picked(all.size(), false);
    QVector<int> rows;
    for (int row = 0; row < m_list->topLevelItemCount(); ++row)
        if (m_list->topLevelItem(row)->isSelected()) { picked[row] = true; rows.append(row); }
    if (rows.isEmpty()) return;

    QVector<Clip> moved;
    QVector<int> newRows;
    if (delta < -1 || delta > 1) {
        // To the top or bottom, keeping their order among themselves.
        QVector<Clip> chosen, rest;
        for (int row = 0; row < all.size(); ++row) (picked[row] ? chosen : rest).append(all[row]);
        moved = delta < 0 ? chosen + rest : rest + chosen;
        const int first = delta < 0 ? 0 : int(rest.size());
        for (int i = 0; i < chosen.size(); ++i) newRows.append(first + i);
    } else {
        // One place, each clip stopping when it meets the edge or another
        // selected clip that already stopped.
        moved = all;
        if (delta < 0) {
            int limit = 0;
            for (int row : rows) {
                const int target = std::max(row - 1, limit);
                if (target < row) std::swap(moved[target], moved[row]);
                newRows.append(target);
                limit = target + 1;
            }
        } else {
            int limit = int(all.size()) - 1;
            for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
                const int row = *it;
                const int target = std::min(row + 1, limit);
                if (target > row) std::swap(moved[target], moved[row]);
                newRows.prepend(target);
                limit = target - 1;
            }
        }
    }
    fill(moved);
    selectRows(newRows);
}

void ArrangePlaylistDialog::setSelectedCategory(const QString& category)
{
    QString name = category.trimmed();
    for (const QString& existing : std::as_const(m_categories))
        if (existing.compare(name, Qt::CaseInsensitive) == 0) name = existing;
    if (!name.isEmpty() && !m_categories.contains(name)) {
        m_categories.append(name);
        refreshCategoryChoices();
    }
    for (QTreeWidgetItem* item : m_list->selectedItems()) item->setText(CategoryColumn, name);
    m_list->header()->resizeSections(QHeaderView::ResizeToContents);
}

void ArrangePlaylistDialog::refreshCategoryChoices()
{
    const QString current = m_category->currentData().toString();
    const QSignalBlocker blocker(m_category);
    m_category->clear();
    m_category->addItem(tr("(No category)"), QString());
    for (const QString& name : std::as_const(m_categories)) m_category->addItem(name, name);
    const int index = m_category->findData(current);
    m_category->setCurrentIndex(index < 0 ? 0 : index);
    refreshButtons();
}

void ArrangePlaylistDialog::refreshButtons()
{
    const bool anySelected = !m_list->selectedItems().isEmpty();
    for (QPushButton* button : std::as_const(m_moveButtons)) button->setEnabled(anySelected);
    m_setCategory->setEnabled(anySelected);
    const bool named = !m_category->currentData().toString().isEmpty();
    m_renameCategory->setEnabled(named);
    m_removeCategory->setEnabled(named);
}

void ArrangePlaylistDialog::addCategory()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Category"), tr("Category name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QString existing = name;
    for (const QString& category : std::as_const(m_categories))
        if (category.compare(name, Qt::CaseInsensitive) == 0) existing = category;
    if (!m_categories.contains(existing)) m_categories.append(existing);
    refreshCategoryChoices();
    m_category->setCurrentIndex(m_category->findData(existing));
}

void ArrangePlaylistDialog::renameCategory()
{
    const QString old = m_category->currentData().toString();
    if (old.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Category"), tr("Category name:"),
                                               QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name.isEmpty() || name == old) return;
    for (const QString& category : std::as_const(m_categories))
        if (category != old && category.compare(name, Qt::CaseInsensitive) == 0) return; // taken
    m_categories[m_categories.indexOf(old)] = name;
    for (int row = 0; row < m_list->topLevelItemCount(); ++row)
        if (m_list->topLevelItem(row)->text(CategoryColumn) == old) m_list->topLevelItem(row)->setText(CategoryColumn, name);
    refreshCategoryChoices();
    m_category->setCurrentIndex(m_category->findData(name));
}

void ArrangePlaylistDialog::removeCategory()
{
    const QString name = m_category->currentData().toString();
    if (name.isEmpty()) return;
    m_categories.removeAll(name);
    for (int row = 0; row < m_list->topLevelItemCount(); ++row)
        if (m_list->topLevelItem(row)->text(CategoryColumn) == name) m_list->topLevelItem(row)->setText(CategoryColumn, QString());
    refreshCategoryChoices();
}

} // namespace atk::ui
