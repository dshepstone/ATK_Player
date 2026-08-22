#include "ui/CompareBar.h"
#include "core/commands/CommandId.h"
#include "project/Project.h"
#include "ui/commands/CommandRegistry.h"
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QStandardItemModel>

namespace atk::ui {
CompareBar::CompareBar(CommandRegistry* commands, QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("CompareBar"));
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(8, 4, 8, 4); layout->setSpacing(6);
    layout->addWidget(new QLabel(tr("A"), this)); m_sourceA = new QComboBox(this); m_sourceA->setObjectName(QStringLiteral("CompareSourceA")); layout->addWidget(m_sourceA, 1);
    layout->addWidget(new QLabel(tr("B"), this)); m_sourceB = new QComboBox(this); m_sourceB->setObjectName(QStringLiteral("CompareSourceB")); layout->addWidget(m_sourceB, 1);
    for (auto id : {commands::CommandId::CompareSideBySide, commands::CommandId::CompareStacked}) {
        auto* button = new QToolButton(this); button->setDefaultAction(commands->action(id)); layout->addWidget(button);
    }
    connect(m_sourceA, &QComboBox::currentIndexChanged, this, [this](int index) { if (index >= 0) emit sourceASelected(m_sourceA->itemData(index).toUuid()); });
    connect(m_sourceB, &QComboBox::currentIndexChanged, this, [this](int index) { if (index >= 0) emit sourceBSelected(m_sourceB->itemData(index).toUuid()); });
}
void CompareBar::setProject(project::Project* project) { m_project = project; }
void CompareBar::refresh(const QUuid& a, const QUuid& b)
{
    QSignalBlocker blockA(m_sourceA), blockB(m_sourceB); m_sourceA->clear(); m_sourceB->clear();
    if (!m_project) return;
    for (const auto& entry : m_project->entries()) {
        QString name = entry.displayName;
        const bool usable = entry.source && entry.availability != project::SourceAvailability::Missing
            && entry.availability != project::SourceAvailability::Error;
        if (!usable) name += tr(" (Unavailable)");
        for (QComboBox* combo : {m_sourceA, m_sourceB}) {
            combo->addItem(name, entry.id);
            if (auto* model = qobject_cast<QStandardItemModel*>(combo->model()))
                if (auto* item = model->item(combo->count() - 1)) item->setEnabled(usable);
        }
    }
    m_sourceA->setCurrentIndex(m_project->indexForId(a)); m_sourceB->setCurrentIndex(m_project->indexForId(b));
}
} // namespace atk::ui
