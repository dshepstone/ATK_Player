#include "ui/SourcesPanel.h"

#include "media/MediaSource.h"
#include "project/Project.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace atk::ui {

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
                this, [this](int index) { m_list->setCurrentRow(index); });
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
                auto* item = new QListWidgetItem(QStringLiteral("%1  %2%3").arg(order++).arg(name,
                    entry.missing ? tr("  [Missing]") : QString()), m_list);
                item->setData(Qt::UserRole, entry.id);
                if (entry.missing) item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
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
