#include "ui/CompareBar.h"
#include "core/commands/CommandId.h"
#include "project/Project.h"
#include "playback/CompareSession.h"
#include "ui/commands/CommandRegistry.h"
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QStandardItemModel>
#include <QFileInfo>

namespace atk::ui {
CompareBar::CompareBar(CommandRegistry* commands, QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("CompareBar"));
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(8, 4, 8, 4); layout->setSpacing(6);
    layout->addWidget(new QLabel(tr("A"), this)); m_sourceA = new QComboBox(this); m_sourceA->setObjectName(QStringLiteral("CompareSourceA")); layout->addWidget(m_sourceA, 1);
    layout->addWidget(new QLabel(tr("B"), this)); m_sourceB = new QComboBox(this); m_sourceB->setObjectName(QStringLiteral("CompareSourceB")); layout->addWidget(m_sourceB, 1);
    layout->addWidget(new QLabel(tr("Audio"), this));
    m_audioMode = new QComboBox(this); m_audioMode->setObjectName(QStringLiteral("CompareAudioMode"));
    m_audioMode->addItem(tr("A"), static_cast<int>(playback::CompareAudioMode::SourceA));
    m_audioMode->addItem(tr("B"), static_cast<int>(playback::CompareAudioMode::SourceB));
    m_audioMode->addItem(tr("External"), static_cast<int>(playback::CompareAudioMode::External));
    layout->addWidget(m_audioMode);
    m_externalName = new QLabel(tr("No External Audio Loaded"), this);
    m_externalName->setObjectName(QStringLiteral("CompareExternalAudioName"));
    m_externalName->setMaximumWidth(150); layout->addWidget(m_externalName);
    for (auto id : {commands::CommandId::LoadExternalAudio, commands::CommandId::ClearExternalAudio}) {
        auto* button = new QToolButton(this); button->setDefaultAction(commands->action(id)); layout->addWidget(button);
    }
    for (auto id : {commands::CommandId::CompareSideBySide, commands::CommandId::CompareStacked}) {
        auto* button = new QToolButton(this); button->setDefaultAction(commands->action(id)); layout->addWidget(button);
    }
    connect(m_sourceA, &QComboBox::currentIndexChanged, this, [this](int index) { if (index >= 0) emit sourceASelected(m_sourceA->itemData(index).toUuid()); });
    connect(m_sourceB, &QComboBox::currentIndexChanged, this, [this](int index) { if (index >= 0) emit sourceBSelected(m_sourceB->itemData(index).toUuid()); });
    connect(m_audioMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) emit audioModeSelected(static_cast<playback::CompareAudioMode>(m_audioMode->itemData(index).toInt()));
    });
}
void CompareBar::setAudioState(playback::CompareAudioMode mode, const QString& path,
                               bool aHasAudio, bool bHasAudio)
{
    QSignalBlocker blocker(m_audioMode);
    m_audioMode->setItemText(0, aHasAudio ? tr("A") : tr("A (No Audio)"));
    m_audioMode->setItemText(1, bHasAudio ? tr("B") : tr("B (No Audio)"));
    m_audioMode->setCurrentIndex(m_audioMode->findData(static_cast<int>(mode)));
    const QString name = path.isEmpty() ? tr("No External Audio Loaded") : QFileInfo(path).fileName();
    m_externalName->setText(name); m_externalName->setToolTip(path);
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
