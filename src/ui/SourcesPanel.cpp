#include "ui/SourcesPanel.h"

#include "ui/IconProvider.h"
#include "ui/PlaylistCardDelegate.h"
#include "ui/Theme.h"

#include "media/MediaSource.h"
#include "project/Project.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>

namespace atk::ui {
namespace {
QString durationText(qint64 durationUs)
{
    if (durationUs < 0) return {};
    const qint64 total = durationUs / 1'000'000;
    const qint64 hours = total / 3600, minutes = (total / 60) % 60, seconds = total % 60;
    return hours > 0 ? QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString metadataText(const media::MediaMetadata& metadata)
{
    QStringList parts;
    if (metadata.resolution.isValid()) parts << QStringLiteral("%1×%2").arg(metadata.resolution.width()).arg(metadata.resolution.height());
    if (metadata.frameRate.isValid()) {
        QString fps = QString::number(metadata.frameRate.toDouble(), 'f',
                                      metadata.frameRate.denominator == 1 ? 0 : 3);
        while (fps.contains(QLatin1Char('.')) && fps.endsWith(QLatin1Char('0'))) fps.chop(1);
        if (fps.endsWith(QLatin1Char('.'))) fps.chop(1);
        parts << QStringLiteral("%1 fps").arg(fps);
    }
    if (metadata.durationUs >= 0) parts << durationText(metadata.durationUs);
    return parts.join(QStringLiteral(" · "));
}
}

SourcesPanel::SourcesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* heading = new QLabel(tr("PLAYLIST"), this);
    heading->setProperty("atkRole", "panelHeading");
    layout->addWidget(heading);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("PlaylistList"));
    m_list->setAccessibleName(tr("Playlist"));
    m_list->setItemDelegate(new PlaylistCardDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setAttribute(Qt::WA_Hover, true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);
    // Files dropped anywhere on the window are added to the playlist.
    m_dropHint = new QLabel(tr("Drop clips here to add them"), this);
    m_dropHint->setObjectName(QStringLiteral("PlaylistDropHint"));
    m_dropHint->setProperty("atkRole", "placeholder");
    m_dropHint->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_dropHint);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(4, 4, 4, 4);
    auto* add = new QPushButton(icons::icon(QStringLiteral("plus")), QString(), this); add->setToolTip(tr("Add media to playlist"));
    auto* remove = new QPushButton(icons::icon(QStringLiteral("minus")), QString(), this); remove->setToolTip(tr("Remove selected playlist item"));
    auto* up = new QPushButton(icons::icon(QStringLiteral("arrow-up")), QString(), this); up->setToolTip(tr("Move selected item up"));
    auto* down = new QPushButton(icons::icon(QStringLiteral("arrow-down")), QString(), this); down->setToolTip(tr("Move selected item down"));
    auto* arrange = new QPushButton(icons::icon(QStringLiteral("list-ordered")), QString(), this);
    arrange->setObjectName(QStringLiteral("PlaylistArrangeButton"));
    arrange->setToolTip(tr("Arrange the playlist: move several clips at once, sort by name, date or length, "
                           "and file clips under categories"));
    for (QPushButton* button : {add, remove, up, down, arrange}) {
        button->setProperty("atkRole", "iconButton");
        button->setAccessibleName(button->toolTip());
        button->setIconSize(QSize(theme::metrics::iconSmall, theme::metrics::iconSmall));
        button->setFixedWidth(30);
        controls->addWidget(button);
    }
    controls->insertStretch(controls->indexOf(arrange), 1);
    layout->addLayout(controls);
    connect(add, &QPushButton::clicked, this, &SourcesPanel::addMediaRequested);
    connect(remove, &QPushButton::clicked, this, [this] { emit removeRequested(selectedIndex()); });
    connect(up, &QPushButton::clicked, this, [this] { emit moveRequested(selectedIndex(), selectedIndex() - 1); });
    connect(down, &QPushButton::clicked, this, [this] { emit moveRequested(selectedIndex(), selectedIndex() + 1); });
    connect(arrange, &QPushButton::clicked, this, &SourcesPanel::arrangeRequested);

    // The hint replaces the list rather than sitting beneath it, so the empty
    // state reads as one panel instead of an empty box with a caption below it.
    m_emptyHint = new QLabel(tr("No clips in playlist.\nDrop clips here, or click + to add media."), this);
    m_emptyHint->setProperty("atkRole", "placeholder");
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_emptyHint, 1);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            emit sourceActivated(m_list->row(item));
        }
    });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int first, int, const QModelIndex&, int destination) {
                if (m_refreshing) return;
                const int target = destination > first ? destination - 1 : destination;
                emit moveRequested(first, target);
            });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        if (QListWidgetItem* item = m_list->itemAt(position)) m_list->setCurrentItem(item);
        if (selectedIndex() < 0) return;
        QMenu menu(this);
        QAction* activate = menu.addAction(tr("Activate"));
        QAction* relink = menu.addAction(tr("Relink Media..."));
        // Only image sources have a rate of their own; a video's comes from the file.
        QAction* frameRate = nullptr;
        if (m_project != nullptr && selectedIndex() < m_project->entries().size()) {
            const auto& source = m_project->entries().at(selectedIndex()).source;
            if (source && (source->isImageSequence() || source->isStillImage()))
                frameRate = menu.addAction(tr("Frame Rate..."));
        }
        // Category: none, one of the project's, or a new one.
        QMenu* category = nullptr;
        QAction* newCategory = nullptr;
        if (m_project != nullptr && selectedIndex() < m_project->entries().size()) {
            const QString current = m_project->entries().at(selectedIndex()).category;
            category = menu.addMenu(icons::icon(QStringLiteral("tag")), tr("Category"));
            category->setObjectName(QStringLiteral("PlaylistCategoryMenu"));
            const auto addChoice = [category, &current](const QString& text, const QString& name) {
                QAction* choice = category->addAction(text);
                choice->setData(name);
                choice->setCheckable(true);
                choice->setChecked(name == current);
            };
            addChoice(tr("None"), QString());
            if (!m_project->categories().isEmpty()) category->addSeparator();
            for (const QString& name : m_project->categories()) addChoice(name, name);
            category->addSeparator();
            newCategory = category->addAction(tr("New Category…"));
        }
        QAction* arrangeAction = menu.addAction(icons::icon(QStringLiteral("list-ordered")), tr("Arrange Playlist…"));
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Remove"));
        QAction* selected = menu.exec(m_list->viewport()->mapToGlobal(position));
        if (selected != nullptr && selected == newCategory) {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("New Category"), tr("Category name:"),
                                                       QLineEdit::Normal, QString(), &ok).trimmed();
            if (ok && !name.isEmpty()) emit categoryRequested(selectedIndex(), name);
            return;
        }
        if (selected != nullptr && category != nullptr && selected->parent() == category) {
            emit categoryRequested(selectedIndex(), selected->data().toString());
            return;
        }
        if (selected == arrangeAction) { emit arrangeRequested(); return; }
        if (selected == activate) emit sourceActivated(selectedIndex());
        else if (selected == relink) emit relinkRequested(selectedIndex());
        else if (selected != nullptr && selected == frameRate) emit frameRateRequested(selectedIndex());
        else if (selected == remove) emit removeRequested(selectedIndex());
    });

    setMinimumWidth(200);
    refresh();
}

int SourcesPanel::selectedIndex() const { return m_list->currentRow(); }

SourcesPanel::~SourcesPanel() = default;

void SourcesPanel::setProject(project::Project* project)
{
    if (m_project == project) {
        return;
    }

    if (m_project != nullptr) {
        m_project->disconnect(this);
    }

    m_project = project;

    if (m_project != nullptr) {
        connect(m_project, &project::Project::entriesChanged,
                this, &SourcesPanel::refresh);
        connect(m_project, &project::Project::activeIndexChanged,
                this, [this](int) { refresh(); });
        // Drawing and notes change only the counts: update the card in place
        // rather than rebuilding the list (which would lose the scroll position).
        connect(m_project, &project::Project::annotationsChanged,
                this, [this](const QUuid& id) { updateCounts(id); });
        connect(m_project, &project::Project::clipStatusChanged, this, [this](const QUuid& id) {
            const int index = m_project->indexForId(id);
            if (index >= 0 && index < m_list->count() && m_list->item(index)->data(Qt::UserRole).toUuid() == id)
                m_list->item(index)->setData(playlistrole::Status, int(m_project->entries().at(index).status));
        });
    }

    refresh();
}

void SourcesPanel::setCurrentMedia(const QString& fileName, const QString& description)
{
    m_showingSingleSource = true;

    m_list->clear();

    auto* item = new QListWidgetItem(fileName, m_list);
    item->setData(playlistrole::Order, 1);
    item->setData(playlistrole::Active, true);
    item->setData(playlistrole::Subtitle, description);
    item->setToolTip(description.isEmpty() ? fileName : fileName + QLatin1Char('\n') + description);
    m_list->setCurrentRow(0);

    m_emptyHint->setVisible(false);
    m_dropHint->setVisible(false);
    m_list->setVisible(true);
}

void SourcesPanel::clearCurrentMedia()
{
    m_showingSingleSource = false;
    refresh();
}

void SourcesPanel::refresh()
{
    if (m_showingSingleSource) {
        // A single loaded source takes precedence over the (empty) project.
        return;
    }

    m_refreshing = true;
    m_list->clear();

    if (m_project != nullptr) {
        int order = 1;
        for (const project::SourceEntry& entry : m_project->entries()) {
            if (entry.source) {
                const QString name = entry.displayName.isEmpty() ? entry.source->displayName() : entry.displayName;
                QString state;
                switch (entry.availability) {
                case project::SourceAvailability::Unknown: state = tr("Unknown"); break;
                case project::SourceAvailability::Probing: state = tr("Probing…"); break;
                case project::SourceAvailability::Ready: state = metadataText(entry.source->metadata()); break;
                case project::SourceAvailability::Missing: state = tr("Missing"); break;
                case project::SourceAvailability::Error: state = tr("Unreadable"); break;
                }
                const bool current = m_project->activeIndex() == order - 1;
                const bool unavailable = entry.availability == project::SourceAvailability::Missing
                    || entry.availability == project::SourceAvailability::Error;
                auto* item = new QListWidgetItem(name, m_list);
                item->setData(Qt::UserRole, entry.id);
                item->setData(playlistrole::Order, order);
                item->setData(playlistrole::Subtitle, state);
                item->setData(playlistrole::Active, current);
                item->setData(playlistrole::Unavailable, unavailable);
                item->setData(playlistrole::FrameCount,
                              entry.availability == project::SourceAvailability::Ready
                                  ? qint64(entry.source->metadata().effectiveFrameCount()) : qint64(0));
                item->setData(playlistrole::Thumbnail, m_thumbnails.value(entry.id));
                item->setData(playlistrole::Status, int(entry.status));
                item->setData(playlistrole::Category, entry.category);
                item->setData(Qt::AccessibleTextRole,
                              QStringLiteral("%1. %2%3").arg(order).arg(name, state.isEmpty() ? QString() : QStringLiteral(", ") + state));
                item->setToolTip(entry.availabilityError.isEmpty() ? entry.source->filePath()
                    : QStringLiteral("%1\n%2").arg(entry.source->filePath(), entry.availabilityError));
                updateCounts(item, entry);
                ++order;
                // Ask once per clip and path; a relink to another file asks again.
                const QString requestKey = entry.id.toString() + entry.source->filePath();
                if (entry.availability == project::SourceAvailability::Ready && !m_thumbnailsRequested.contains(requestKey)) {
                    m_thumbnailsRequested.insert(requestKey);
                    emit thumbnailWanted(entry.id);
                }
            }
        }
        m_list->setCurrentRow(m_project->activeIndex());
    }

    const bool empty = m_list->count() == 0;
    m_emptyHint->setVisible(empty);
    m_list->setVisible(!empty);
    m_dropHint->setVisible(!empty);
    m_refreshing = false;
}

void SourcesPanel::updateCounts(QListWidgetItem* item, const project::SourceEntry& entry)
{
    int notes = 0;
    int drawn = 0;
    for (const annotation::FrameAnnotation& frame : entry.annotations.frames()) {
        notes += int(frame.notes.size());
        if (frame.hasDrawings()) ++drawn;
    }
    item->setData(playlistrole::NoteCount, notes);
    item->setData(playlistrole::DrawnFrames, drawn);
}

void SourcesPanel::updateCounts(const QUuid& sourceId)
{
    if (m_showingSingleSource || m_project == nullptr) return;
    const int index = m_project->indexForId(sourceId);
    if (index < 0 || index >= m_list->count()) return;
    QListWidgetItem* item = m_list->item(index);
    if (item->data(Qt::UserRole).toUuid() != sourceId) return;
    updateCounts(item, m_project->entries().at(index));
}

void SourcesPanel::setThumbnail(const QUuid& sourceId, const QImage& image)
{
    if (image.isNull()) m_thumbnails.remove(sourceId);
    else m_thumbnails.insert(sourceId, image);
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (item->data(Qt::UserRole).toUuid() == sourceId) item->setData(playlistrole::Thumbnail, image);
    }
}

} // namespace atk::ui
