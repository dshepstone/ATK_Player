#include "ui/SourcesPanel.h"

#include "media/MediaSource.h"
#include "project/Project.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace atk::ui {

SourcesPanel::SourcesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* heading = new QLabel(tr("SOURCES"), this);
    heading->setProperty("atkRole", "panelHeading");
    layout->addWidget(heading);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    layout->addWidget(m_list, 1);

    // The hint replaces the list rather than sitting beneath it, so the empty
    // state reads as one panel instead of an empty box with a caption below it.
    m_emptyHint = new QLabel(tr("No sources yet.\nOpening media arrives in milestone M1."), this);
    m_emptyHint->setProperty("atkRole", "placeholder");
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_emptyHint, 1);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            emit sourceActivated(m_list->row(item));
        }
    });

    setMinimumWidth(200);
    refresh();
}

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

void SourcesPanel::refresh()
{
    m_list->clear();

    if (m_project != nullptr) {
        for (const project::SourceEntry& entry : m_project->entries()) {
            if (entry.source) {
                m_list->addItem(entry.source->displayName());
            }
        }
        m_list->setCurrentRow(m_project->activeIndex());
    }

    const bool empty = m_list->count() == 0;
    m_emptyHint->setVisible(empty);
    m_list->setVisible(!empty);
}

} // namespace atk::ui
