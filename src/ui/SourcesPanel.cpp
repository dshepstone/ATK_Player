#include "ui/SourcesPanel.h"

#include "media/MediaSource.h"
#include "project/Project.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
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
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(4, 4, 4, 4);
    auto* add = new QPushButton(tr("+"), this); add->setToolTip(tr("Add media to playlist"));
    auto* remove = new QPushButton(tr("−"), this); remove->setToolTip(tr("Remove selected playlist item"));
    auto* up = new QPushButton(tr("↑"), this); up->setToolTip(tr("Move selected item up"));
    auto* down = new QPushButton(tr("↓"), this); down->setToolTip(tr("Move selected item down"));
    for (QPushButton* button : {add, remove, up, down}) { button->setFixedWidth(30); controls->addWidget(button); }
    controls->addStretch(1); layout->addLayout(controls);
    connect(add, &QPushButton::clicked, this, &SourcesPanel::addMediaRequested);
    connect(remove, &QPushButton::clicked, this, [this] { emit removeRequested(selectedIndex()); });
    connect(up, &QPushButton::clicked, this, [this] { emit moveRequested(selectedIndex(), selectedIndex() - 1); });
    connect(down, &QPushButton::clicked, this, [this] { emit moveRequested(selectedIndex(), selectedIndex() + 1); });

    // The hint replaces the list rather than sitting beneath it, so the empty
    // state reads as one panel instead of an empty box with a caption below it.
    m_emptyHint = new QLabel(tr("No clips in playlist.\nAdd media to begin."), this);
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
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Remove"));
        QAction* selected = menu.exec(m_list->viewport()->mapToGlobal(position));
        if (selected == activate) emit sourceActivated(selectedIndex());
        else if (selected == relink) emit relinkRequested(selectedIndex());
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
    }

    refresh();
}

void SourcesPanel::setCurrentMedia(const QString& fileName, const QString& description)
{
    m_showingSingleSource = true;

    m_list->clear();

    auto* item = new QListWidgetItem(fileName, m_list);
    if (!description.isEmpty()) {
        // Second line carries the resolution/fps/duration summary.
        const QString label = fileName + QLatin1Char('\n') + description;
        item->setText(label);
        item->setToolTip(label);
    }
    m_list->setCurrentRow(0);

    m_emptyHint->setVisible(false);
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
                auto* item = new QListWidgetItem(QStringLiteral("%1 %2  %3%4").arg(current ? QStringLiteral("▶") : QStringLiteral(" "))
                    .arg(order++).arg(name, state.isEmpty() ? QString() : QStringLiteral(" — ") + state), m_list);
                item->setData(Qt::UserRole, entry.id);
                item->setToolTip(entry.availabilityError.isEmpty() ? entry.source->filePath()
                    : QStringLiteral("%1\n%2").arg(entry.source->filePath(), entry.availabilityError));
                QFont font = item->font(); font.setBold(current); item->setFont(font);
                if (entry.availability == project::SourceAvailability::Missing
                    || entry.availability == project::SourceAvailability::Error)
                    item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            }
        }
        m_list->setCurrentRow(m_project->activeIndex());
    }

    const bool empty = m_list->count() == 0;
    m_emptyHint->setVisible(empty);
    m_list->setVisible(!empty);
    m_refreshing = false;
}

} // namespace atk::ui
