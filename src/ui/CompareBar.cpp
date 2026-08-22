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
#include <QFrame>
#include <QFont>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace atk::ui {
namespace {
QLabel* sectionLabel(const QString& text, const QString& objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(std::max(7.0, font.pointSizeF() - 1.0));
    label->setFont(font);
    return label;
}

QFrame* separator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setFixedWidth(9);
    return line;
}

class SignedFrameSpinBox final : public QSpinBox {
public:
    using QSpinBox::QSpinBox;
protected:
    QString textFromValue(int value) const override
    {
        return QStringLiteral("%1%2f").arg(value > 0 ? QStringLiteral("+") : QString(),
                                           QString::number(value));
    }
    int valueFromText(const QString& text) const override
    {
        QString value = text.trimmed();
        if (value.endsWith(QLatin1Char('f'), Qt::CaseInsensitive)) value.chop(1);
        return value.toInt();
    }
};
}

CompareBar::CompareBar(CommandRegistry* commands, QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("CompareBar"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* primaryRow = new QWidget(this);
    auto* layout = new QHBoxLayout(primaryRow);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(4);
    outer->addWidget(primaryRow);

    layout->addWidget(sectionLabel(tr("SOURCES"), QStringLiteral("CompareSourcesSection"), this));
    auto* aLabel = sectionLabel(tr("A"), QStringLiteral("CompareSourceALabel"), this);
    auto* bLabel = sectionLabel(tr("B"), QStringLiteral("CompareSourceBLabel"), this);
    aLabel->setFixedWidth(12); bLabel->setFixedWidth(12);
    layout->addWidget(aLabel);
    m_sourceA = new QComboBox(this); m_sourceA->setObjectName(QStringLiteral("CompareSourceA"));
    m_sourceA->setMinimumWidth(76); m_sourceA->setMaximumWidth(110);
    m_sourceA->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sourceA->setToolTip(tr("Source A")); m_sourceA->setAccessibleName(tr("Source A"));
    layout->addWidget(m_sourceA, 1);
    layout->addWidget(bLabel);
    m_sourceB = new QComboBox(this); m_sourceB->setObjectName(QStringLiteral("CompareSourceB"));
    m_sourceB->setMinimumWidth(76); m_sourceB->setMaximumWidth(110);
    m_sourceB->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sourceB->setToolTip(tr("Source B")); m_sourceB->setAccessibleName(tr("Source B"));
    layout->addWidget(m_sourceB, 1);
    layout->addWidget(separator(this));

    layout->addWidget(sectionLabel(tr("AUDIO"), QStringLiteral("CompareAudioSection"), this));
    m_audioMode = new QComboBox(this); m_audioMode->setObjectName(QStringLiteral("CompareAudioMode"));
    m_audioMode->addItem(tr("A"), static_cast<int>(playback::CompareAudioMode::SourceA));
    m_audioMode->addItem(tr("B"), static_cast<int>(playback::CompareAudioMode::SourceB));
    m_audioMode->addItem(tr("External"), static_cast<int>(playback::CompareAudioMode::External));
    m_audioMode->setToolTip(tr("Comparison Audio Source"));
    m_audioMode->setAccessibleName(tr("Comparison Audio Source"));
    layout->addWidget(m_audioMode);
    layout->addStretch(1);

    m_externalControls = new QWidget(this);
    m_externalControls->setObjectName(QStringLiteral("CompareExternalAudioControls"));
    auto* externalLayout = new QHBoxLayout(m_externalControls);
    externalLayout->setContentsMargins(10, 0, 10, 5);
    externalLayout->setSpacing(4);
    externalLayout->addWidget(sectionLabel(tr("EXTERNAL AUDIO"),
                                           QStringLiteral("CompareExternalAudioSection"),
                                           m_externalControls));
    m_externalName = new QLabel(tr("No External Audio Loaded"), this);
    m_externalName->setObjectName(QStringLiteral("CompareExternalAudioName"));
    m_externalName->setMinimumWidth(80); m_externalName->setMaximumWidth(160);
    m_externalName->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_externalName->setAccessibleName(tr("Loaded External Audio"));
    externalLayout->addWidget(m_externalName);
    for (auto id : {commands::CommandId::LoadExternalAudio, commands::CommandId::ClearExternalAudio}) {
        auto* button = new QToolButton(this); button->setDefaultAction(commands->action(id));
        const bool load = id == commands::CommandId::LoadExternalAudio;
        button->setObjectName(load ? QStringLiteral("CompareLoadExternalAudio")
                                   : QStringLiteral("CompareClearExternalAudio"));
        button->setText(load ? tr("Load…") : tr("Clear"));
        button->setToolTip(load ? tr("Load External Audio") : tr("Clear External Audio"));
        button->setAccessibleName(button->toolTip());
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(commands->action(id), &QAction::changed, button, [button, load] {
            button->setText(load ? QObject::tr("Load…") : QObject::tr("Clear"));
        });
        externalLayout->addWidget(button);
    }
    externalLayout->addWidget(separator(m_externalControls));
    externalLayout->addWidget(sectionLabel(tr("VIEW"), QStringLiteral("CompareLayoutSection"),
                                           m_externalControls));
    m_viewMode = new QComboBox(m_externalControls);
    m_viewMode->setObjectName(QStringLiteral("CompareViewMode"));
    m_viewMode->addItem(tr("Side by Side"), static_cast<int>(playback::CompareLayout::SideBySide));
    m_viewMode->addItem(tr("Stacked"), static_cast<int>(playback::CompareLayout::Stacked));
    m_viewMode->addItem(tr("Wipe"), static_cast<int>(playback::CompareLayout::Wipe));
    m_viewMode->addItem(tr("Blend"), static_cast<int>(playback::CompareLayout::Blend));
    m_viewMode->addItem(tr("Difference"), static_cast<int>(playback::CompareLayout::Difference));
    externalLayout->addWidget(m_viewMode);
    externalLayout->addStretch(1);
    outer->addWidget(m_externalControls);

    auto* toolsRow = new QWidget(this);
    toolsRow->setObjectName(QStringLiteral("CompareToolsRow"));
    auto* tools = new QHBoxLayout(toolsRow);
    tools->setContentsMargins(10, 0, 10, 5); tools->setSpacing(4);
    const auto addOffset = [this, tools](const QString& title, const QString& objectName,
                                         const QString& tooltip, QSpinBox*& field) {
        tools->addWidget(sectionLabel(title, objectName + QStringLiteral("Label"), this));
        auto* minus = new QToolButton(this); minus->setText(tr("−1f"));
        minus->setObjectName(objectName + QStringLiteral("Minus"));
        field = new SignedFrameSpinBox(this); field->setObjectName(objectName);
        field->setRange(-100000, 100000);
        field->setKeyboardTracking(false); field->setFixedWidth(78); field->setToolTip(tooltip);
        auto* plus = new QToolButton(this); plus->setText(tr("+1f"));
        plus->setObjectName(objectName + QStringLiteral("Plus"));
        auto* reset = new QToolButton(this); reset->setText(tr("Reset"));
        reset->setObjectName(objectName + QStringLiteral("Reset"));
        tools->addWidget(minus); tools->addWidget(field); tools->addWidget(plus); tools->addWidget(reset);
        connect(minus, &QToolButton::clicked, field, [field] { field->setValue(field->value() - 1); });
        connect(plus, &QToolButton::clicked, field, [field] { field->setValue(field->value() + 1); });
        connect(reset, &QToolButton::clicked, field, [field] { field->setValue(0); });
    };
    addOffset(tr("B OFFSET"), QStringLiteral("CompareBOffsetFrames"),
              tr("Positive values advance B later into its source timeline relative to A. Time is authoritative; frames use Source A's rational rate."), m_bOffset);
    tools->addWidget(separator(toolsRow));
    addOffset(tr("AUDIO OFFSET"), QStringLiteral("CompareExternalOffsetFrames"),
              tr("Positive values advance External Audio later into its source timeline relative to A. Time is authoritative; frames use Source A's rational rate."), m_audioOffset);
    m_wipeControls = new QWidget(toolsRow); auto* wipeLayout = new QHBoxLayout(m_wipeControls);
    wipeLayout->setContentsMargins(8, 0, 0, 0); wipeLayout->addWidget(new QLabel(tr("Wipe"), m_wipeControls));
    m_wipe = new QSlider(Qt::Horizontal, m_wipeControls); m_wipe->setObjectName(QStringLiteral("CompareWipePosition")); m_wipe->setRange(0, 100); m_wipe->setValue(50); m_wipe->setFixedWidth(140); wipeLayout->addWidget(m_wipe);
    tools->addWidget(m_wipeControls);
    m_blendControls = new QWidget(toolsRow); auto* blendLayout = new QHBoxLayout(m_blendControls);
    blendLayout->setContentsMargins(8, 0, 0, 0); blendLayout->addWidget(new QLabel(tr("Blend"), m_blendControls));
    m_blend = new QSlider(Qt::Horizontal, m_blendControls); m_blend->setObjectName(QStringLiteral("CompareBlendAmount")); m_blend->setRange(0, 100); m_blend->setValue(50); m_blend->setFixedWidth(140); blendLayout->addWidget(m_blend);
    tools->addWidget(m_blendControls); tools->addStretch(1); outer->addWidget(toolsRow);
    connect(m_sourceA, &QComboBox::currentIndexChanged, this, [this](int index) { if (index >= 0) emit sourceASelected(m_sourceA->itemData(index).toUuid()); });
    connect(m_sourceB, &QComboBox::currentIndexChanged, this, [this](int index) { if (index >= 0) emit sourceBSelected(m_sourceB->itemData(index).toUuid()); });
    connect(m_audioMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) emit audioModeSelected(static_cast<playback::CompareAudioMode>(m_audioMode->itemData(index).toInt()));
    });
    connect(m_viewMode, &QComboBox::currentIndexChanged, this, [this](int index) { if (index >= 0) emit viewModeSelected(static_cast<playback::CompareLayout>(m_viewMode->itemData(index).toInt())); });
    connect(m_bOffset, &QSpinBox::valueChanged, this, &CompareBar::sourceBOffsetFramesChanged);
    connect(m_audioOffset, &QSpinBox::valueChanged, this, &CompareBar::externalAudioOffsetFramesChanged);
    connect(m_wipe, &QSlider::valueChanged, this, &CompareBar::wipePositionChanged);
    connect(m_blend, &QSlider::valueChanged, this, &CompareBar::blendAmountChanged);
}
void CompareBar::setAudioState(playback::CompareAudioMode mode, const QString& path,
                               bool aHasAudio, bool bHasAudio)
{
    QSignalBlocker blocker(m_audioMode);
    m_audioMode->setItemText(0, aHasAudio ? tr("A") : tr("A (No Audio)"));
    m_audioMode->setItemText(1, bHasAudio ? tr("B") : tr("B (No Audio)"));
    m_audioMode->setCurrentIndex(m_audioMode->findData(static_cast<int>(mode)));
    const QString name = path.isEmpty() ? tr("No External Audio Loaded") : QFileInfo(path).fileName();
    m_externalName->setText(name);
    m_externalName->setToolTip(path.isEmpty() ? tr("No External Audio Loaded") : path);
}
void CompareBar::setComparisonState(playback::CompareLayout layout, int bFrames,
                                    int audioFrames, int wipe, int blend, bool externalLoaded)
{
    const QSignalBlocker b0(m_viewMode), b1(m_bOffset), b2(m_audioOffset), b3(m_wipe), b4(m_blend);
    m_viewMode->setCurrentIndex(m_viewMode->findData(static_cast<int>(layout)));
    m_bOffset->setValue(bFrames); m_audioOffset->setValue(audioFrames);
    m_audioOffset->setEnabled(externalLoaded);
    m_wipe->setValue(wipe); m_blend->setValue(blend);
    m_wipeControls->setVisible(layout == playback::CompareLayout::Wipe);
    m_blendControls->setVisible(layout == playback::CompareLayout::Blend);
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
    const auto updateTip = [](QComboBox* combo, const QString& role) {
        const QString full = combo->currentText();
        combo->setToolTip(QStringLiteral("%1: %2").arg(role, full));
    };
    updateTip(m_sourceA, tr("Source A"));
    updateTip(m_sourceB, tr("Source B"));
}
} // namespace atk::ui
