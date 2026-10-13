#include "ui/ReviewLibraryPage.h"

#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QKeyEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace atk::ui {
namespace {

constexpr int kStripLength = 5;
constexpr QSize kThumbSize(112, 63);

QString pageStyleSheet()
{
    return QStringLiteral(R"CSS(
QWidget#ReviewLibraryPage { background-color: %1; }
QListWidget#ReviewSections { background-color: %2; border: none; border-right: 1px solid %3; padding: 12px 8px; }
QListWidget#ReviewSections::item { padding: 8px 10px; border-radius: 4px; }
QListWidget#ReviewSections::item:selected { background-color: %4; border-left: 0; color: %5; }
QLabel#ReviewLibraryTitle { color: %6; font-size: 20px; font-weight: 600; letter-spacing: 1px; }
QFrame#ReviewRow { background-color: %7; border: 1px solid %3; border-radius: 6px; }
QFrame#ReviewRow:hover { background-color: %8; }
QFrame#ReviewRow QLabel { background: transparent; }
QLabel#ReviewRowName { font-size: 14px; font-weight: 600; }
QLabel#ReviewRowMeta { color: %9; }
QLabel#ReviewThumb { background-color: #000000; border-radius: 3px; }
)CSS")
        .arg(theme::windowBackground().name(), theme::panelBackground().name(), theme::panelBorder().name(),
             theme::accentMuted().name(), theme::textPrimary().name(), theme::accent().name(),
             theme::cardBackground().name(), theme::cardHover().name(), theme::textSecondary().name());
}

QToolButton* flatButton(QWidget* parent, const QString& icon, const QString& tip)
{
    auto* button = new QToolButton(parent);
    button->setIcon(icons::icon(icon));
    button->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setProperty("atkRole", "iconButton");
    return button;
}

/// Opens its review on double-click, like a playlist row.
class RowFrame final : public QFrame {
public:
    std::function<void()> onDoubleClick;
    using QFrame::QFrame;

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (onDoubleClick && event->button() == Qt::LeftButton) onDoubleClick();
    }
};

} // namespace

ReviewLibraryPage::ReviewLibraryPage(review::ReviewLibrary* library, QWidget* parent)
    : QWidget(parent), m_library(library)
{
    setObjectName(QStringLiteral("ReviewLibraryPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(pageStyleSheet());

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_sections = new QListWidget(this);
    m_sections->setObjectName(QStringLiteral("ReviewSections"));
    m_sections->setFixedWidth(200);
    m_sections->setFocusPolicy(Qt::TabFocus); // arrow keys switch sections
    m_sections->setAccessibleName(tr("Review sections"));
    for (const auto& [label, icon] : {std::pair{tr("All Reviews"), QStringLiteral("layout-grid")},
                                      std::pair{tr("Starred"), QStringLiteral("star")},
                                      std::pair{tr("Archived"), QStringLiteral("archive")}})
        new QListWidgetItem(icons::icon(icon), label, m_sections);
    m_sections->setCurrentRow(0);
    connect(m_sections, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) setSection(Section(row));
    });
    root->addWidget(m_sections);

    auto* main = new QWidget(this);
    auto* column = new QVBoxLayout(main);
    column->setContentsMargins(28, 22, 28, 16);
    column->setSpacing(14);
    root->addWidget(main, 1);

    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel(tr("REVIEWS"), main);
    title->setObjectName(QStringLiteral("ReviewLibraryTitle"));
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    auto* openProject = new QPushButton(icons::icon(QStringLiteral("folder-open")), tr("Open Project…"), main);
    openProject->setObjectName(QStringLiteral("ReviewLibraryOpenProject"));
    auto* openMedia = new QPushButton(icons::icon(QStringLiteral("film")), tr("Open Clips…"), main);
    openMedia->setObjectName(QStringLiteral("ReviewLibraryOpenMedia"));
    auto* newReview = new QPushButton(icons::icon(QStringLiteral("plus"), theme::textOnAccent()), tr("New Review"), main);
    newReview->setObjectName(QStringLiteral("ReviewLibraryNew"));
    newReview->setProperty("atkRole", "primary");
    newReview->setToolTip(tr("Make a review from a folder of clips"));
    titleRow->addWidget(openProject);
    titleRow->addWidget(openMedia);
    titleRow->addWidget(newReview);
    column->addLayout(titleRow);
    connect(openProject, &QPushButton::clicked, this, &ReviewLibraryPage::openProjectRequested);
    connect(openMedia, &QPushButton::clicked, this, &ReviewLibraryPage::openMediaRequested);
    connect(newReview, &QPushButton::clicked, this, &ReviewLibraryPage::newReviewRequested);

    auto* filterRow = new QHBoxLayout;
    m_search = new QLineEdit(main);
    m_search->setObjectName(QStringLiteral("ReviewLibrarySearch"));
    m_search->setAccessibleName(tr("Search reviews"));
    m_search->setPlaceholderText(tr("Find reviews by name or folder"));
    m_search->setClearButtonEnabled(true);
    m_sort = new QComboBox(main);
    m_sort->setObjectName(QStringLiteral("ReviewLibrarySort"));
    m_sort->addItem(tr("Sort by Latest"), int(Order::Latest));
    m_sort->addItem(tr("Sort by Name"), int(Order::Name));
    filterRow->addWidget(m_search, 1);
    filterRow->addWidget(m_sort);
    column->addLayout(filterRow);
    connect(m_search, &QLineEdit::textChanged, this, &ReviewLibraryPage::refresh);
    connect(m_sort, &QComboBox::currentIndexChanged, this,
            [this](int index) { if (index >= 0) setOrder(Order(m_sort->itemData(index).toInt())); });

    auto* scroll = new QScrollArea(main);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* rowsHost = new QWidget(scroll);
    m_rows = new QVBoxLayout(rowsHost);
    m_rows->setContentsMargins(0, 0, 6, 0);
    m_rows->setSpacing(10);
    m_rows->addStretch(1);
    scroll->setWidget(rowsHost);
    column->addWidget(scroll, 1);

    m_empty = new QLabel(main);
    m_empty->setObjectName(QStringLiteral("ReviewLibraryEmpty"));
    m_empty->setProperty("atkRole", "placeholder");
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    column->addWidget(m_empty);

    refresh();
}

void ReviewLibraryPage::setSection(Section section)
{
    m_section = section;
    if (m_sections->currentRow() != int(section)) m_sections->setCurrentRow(int(section));
    refresh();
}

void ReviewLibraryPage::setOrder(Order order)
{
    m_order = order;
    const int index = m_sort->findData(int(order));
    if (m_sort->currentIndex() != index) m_sort->setCurrentIndex(index);
    refresh();
}

void ReviewLibraryPage::setSearchText(const QString& text)
{
    m_search->setText(text);
}

void ReviewLibraryPage::refresh()
{
    // Clear the old rows (the trailing stretch stays).
    while (m_rows->count() > 1) {
        QLayoutItem* item = m_rows->takeAt(0);
        delete item->widget();
        delete item;
    }
    m_thumbnailLabels.clear();
    m_listed.clear();

    const QString search = m_search->text().trimmed();
    QVector<review::ReviewInfo> shown;
    for (const review::ReviewInfo& r : m_library->reviews()) {
        if (m_section == Section::Archived ? !r.archived : r.archived) continue;
        if (m_section == Section::Starred && !r.starred) continue;
        if (!search.isEmpty() && !r.name.contains(search, Qt::CaseInsensitive)
            && !r.folder.contains(search, Qt::CaseInsensitive))
            continue;
        shown.append(r);
    }
    std::stable_sort(shown.begin(), shown.end(), [this](const review::ReviewInfo& a, const review::ReviewInfo& b) {
        if (m_order == Order::Name) return a.name.localeAwareCompare(b.name) < 0;
        return std::max(a.openedMsUtc, a.createdMsUtc) > std::max(b.openedMsUtc, b.createdMsUtc);
    });
    for (const review::ReviewInfo& r : shown) {
        m_rows->insertWidget(m_rows->count() - 1, makeRow(r));
        m_listed.append(r.id);
    }

    QString empty;
    if (m_library->reviews().isEmpty())
        empty = tr("No reviews yet.\nClick New Review and choose a folder of clips: each clip becomes an item in "
                   "the review, in the order you choose.");
    else if (shown.isEmpty() && !search.isEmpty())
        empty = tr("No reviews match \"%1\".").arg(search);
    else if (shown.isEmpty() && m_section == Section::Starred)
        empty = tr("No starred reviews. Star a review to keep it here.");
    else if (shown.isEmpty() && m_section == Section::Archived)
        empty = tr("Nothing archived.");
    m_empty->setText(empty);
    m_empty->setVisible(!empty.isEmpty());

    // See eventFilter(): the page's buttons take Space and Enter themselves.
    for (QAbstractButton* button : findChildren<QAbstractButton*>()) button->installEventFilter(this);
}

bool ReviewLibraryPage::eventFilter(QObject* watched, QEvent* event)
{
    // Space is the player's Play/Pause shortcut everywhere in the window, so a
    // focused button here would never see it; claim it (and Enter, which
    // plain buttons ignore outside dialogs) so the library works from the
    // keyboard: Tab to a review's Open button, press Space or Enter.
    auto* button = qobject_cast<QAbstractButton*>(watched);
    if (button && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const bool plain = (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
        const bool space = key->key() == Qt::Key_Space;
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (plain && (space || enter)) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
                return true;
            }
            if (enter && !key->isAutoRepeat()) {
                button->animateClick();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QWidget* ReviewLibraryPage::makeRow(const review::ReviewInfo& review)
{
    auto* row = new RowFrame;
    row->setObjectName(QStringLiteral("ReviewRow"));
    row->setProperty("reviewId", review.id);
    row->setAttribute(Qt::WA_Hover, true);
    const QUuid id = review.id;
    row->onDoubleClick = [this, id] { emit openReviewRequested(id); };
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 10, 10);
    layout->setSpacing(14);

    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    auto* name = new QLabel(review.name, row);
    name->setObjectName(QStringLiteral("ReviewRowName"));
    QStringList meta;
    meta << tr("%n clip(s)", nullptr, review.clipCount);
    const qint64 when = std::max(review.openedMsUtc, review.createdMsUtc);
    if (when > 0)
        meta << tr("opened %1").arg(QLocale().toString(QDateTime::fromMSecsSinceEpoch(when).toLocalTime().date(),
                                                         QLocale::ShortFormat));
    auto* metaLabel = new QLabel(meta.join(QStringLiteral("  ·  ")), row);
    metaLabel->setObjectName(QStringLiteral("ReviewRowMeta"));
    auto* folder = new QLabel(QDir::toNativeSeparators(review.folder), row);
    folder->setObjectName(QStringLiteral("ReviewRowMeta"));
    folder->setToolTip(QDir::toNativeSeparators(review.projectPath));
    text->addWidget(name);
    text->addWidget(metaLabel);
    text->addWidget(folder);
    text->addStretch(1);
    layout->addLayout(text, 1);

    // The first clips, as a strip of thumbnails.
    for (int i = 0; i < std::min<int>(kStripLength, int(review.previewClips.size())); ++i) {
        const QString clip = review.previewClips[i];
        auto* thumb = new QLabel(row);
        thumb->setObjectName(QStringLiteral("ReviewThumb"));
        thumb->setFixedSize(kThumbSize);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setToolTip(QFileInfo(clip).fileName());
        m_thumbnailLabels[clip].append(thumb);
        if (const auto it = m_thumbnails.constFind(clip); it != m_thumbnails.cend()) {
            thumb->setPixmap(QPixmap::fromImage(it->scaled(kThumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        } else {
            thumb->setPixmap(icons::pixmap(QStringLiteral("film"), QSize(18, 18), theme::textDisabled(),
                                           devicePixelRatioF()));
            emit thumbnailWanted(clip);
        }
        layout->addWidget(thumb);
    }

    auto* star = flatButton(row, QStringLiteral("star"), review.starred ? tr("Unstar") : tr("Star"));
    star->setObjectName(QStringLiteral("ReviewRowStar"));
    star->setCheckable(true);
    star->setChecked(review.starred);
    connect(star, &QToolButton::clicked, this, [this, id](bool on) {
        if (const review::ReviewInfo* r = m_library->find(id)) {
            review::ReviewInfo changed = *r;
            changed.starred = on;
            m_library->update(changed);
            emit libraryChanged();
            refresh();
        }
    });
    auto* more = flatButton(row, QStringLiteral("ellipsis"), tr("More"));
    more->setObjectName(QStringLiteral("ReviewRowMore"));
    connect(more, &QToolButton::clicked, this, [this, id, more] { showRowMenu(id, more); });
    auto* open = new QPushButton(icons::icon(QStringLiteral("play"), theme::textOnAccent()), tr("Open"), row);
    open->setObjectName(QStringLiteral("ReviewRowOpen"));
    open->setProperty("atkRole", "primary");
    connect(open, &QPushButton::clicked, this, [this, id] { emit openReviewRequested(id); });
    layout->addWidget(star);
    layout->addWidget(more);
    layout->addWidget(open);
    return row;
}

void ReviewLibraryPage::showRowMenu(const QUuid& id, QWidget* anchor)
{
    const review::ReviewInfo* found = m_library->find(id);
    if (!found) return;
    const review::ReviewInfo review = *found;
    QMenu menu(this);
    QAction* open = menu.addAction(icons::icon(QStringLiteral("play")), tr("Open"));
    QAction* folder = menu.addAction(icons::icon(QStringLiteral("folder-open")), tr("Show Folder"));
    menu.addSeparator();
    QAction* archive = menu.addAction(icons::icon(QStringLiteral("archive")),
                                      review.archived ? tr("Move Out of Archive") : tr("Archive"));
    QAction* remove = menu.addAction(icons::icon(QStringLiteral("x")), tr("Remove from Library…"));
    QAction* chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
    if (chosen == open) {
        emit openReviewRequested(id);
    } else if (chosen == folder) {
        emit showFolderRequested(review.folder);
    } else if (chosen == archive) {
        review::ReviewInfo changed = review;
        changed.archived = !review.archived;
        m_library->update(changed);
        emit libraryChanged();
        refresh();
    } else if (chosen == remove) {
        const auto answer = QMessageBox::question(
            this, tr("Remove Review"),
            tr("Remove \"%1\" from the library?\n\nThe clips and the review's project file stay where they are; "
               "you can open the project again later.").arg(review.name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        m_library->remove(id);
        emit libraryChanged();
        refresh();
    }
}

void ReviewLibraryPage::setThumbnail(const QString& clipPath, const QImage& image)
{
    if (image.isNull()) return;
    m_thumbnails.insert(clipPath, image);
    for (QLabel* label : m_thumbnailLabels.value(clipPath))
        label->setPixmap(QPixmap::fromImage(image.scaled(kThumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

} // namespace atk::ui
