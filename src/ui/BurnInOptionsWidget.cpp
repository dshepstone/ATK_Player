#include "ui/BurnInOptionsWidget.h"
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstdlib>

namespace atk::ui {

BurnInOptionsWidget::BurnInOptionsWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto* group = new QGroupBox(tr("Burn-ins"), this);
    auto* choices = new QVBoxLayout(group);
    m_frameNumber = new QCheckBox(tr("Frame number"), group);
    m_bookmarkLabels = new QCheckBox(tr("Bookmark labels"), group);
    m_bookmarkNotes = new QCheckBox(tr("Bookmark notes"), group);
    choices->addWidget(m_frameNumber); choices->addWidget(m_bookmarkLabels); choices->addWidget(m_bookmarkNotes);
    m_annotations = new QCheckBox(tr("Drawings on their frames"), group);
    m_annotations->setObjectName(QStringLiteral("BurnInDrawings"));
    m_annotationNotes = new QCheckBox(tr("Frame notes"), group);
    m_annotationNotes->setObjectName(QStringLiteral("BurnInNotes"));
    choices->addWidget(m_annotations);
    choices->addWidget(m_annotationNotes);

    // After each drawing or note: play straight through (SyncSketch's
    // default), keep it on screen while the video plays, or stop the picture.
    m_afterRow = new QWidget(group);
    auto* after = new QHBoxLayout(m_afterRow);
    after->setContentsMargins(20, 0, 0, 0);
    after->addWidget(new QLabel(tr("At each drawing or note:"), m_afterRow));
    m_afterNote = new QComboBox(m_afterRow);
    m_afterNote->setObjectName(QStringLiteral("BurnInAfterNote"));
    m_afterNote->addItem(tr("Play through"), int(AfterNote::PlayThrough));
    m_afterNote->addItem(tr("Keep it on screen for"), int(AfterNote::KeepOnScreen));
    m_afterNote->addItem(tr("Freeze the picture for"), int(AfterNote::Freeze));
    m_afterNote->setToolTip(tr("Play through: each drawing shows on its own frame only.\n"
                               "Keep on screen: the drawing and note stay up while the video keeps playing.\n"
                               "Freeze: the picture stops on the drawn frame, then plays on (the video gets longer)."));
    after->addWidget(m_afterNote);
    m_holdFrames = new QSpinBox(m_afterRow);
    m_holdFrames->setObjectName(QStringLiteral("BurnInAfterFrames"));
    m_holdFrames->setRange(1, 240);
    m_holdFrames->setValue(24);
    m_holdFrames->setSuffix(tr(" frames"));
    after->addWidget(m_holdFrames);
    after->addStretch(1);
    choices->addWidget(m_afterRow);

    auto* traceRow = new QHBoxLayout;
    traceRow->addWidget(new QLabel(tr("Tracing paper:"), group));
    m_tracingPaper = new QComboBox(group);
    m_tracingPaper->setObjectName(QStringLiteral("BurnInTracingPaper"));
    for (int level : {0, 25, 50, 75, 100})
        m_tracingPaper->addItem(level == 0 ? tr("Off") : QStringLiteral("%1%").arg(level), level);
    m_tracingPaper->setToolTip(tr("Fade the picture under the drawings, as tracing paper does in the viewer"));
    traceRow->addWidget(m_tracingPaper);
    traceRow->addStretch(1);
    choices->addLayout(traceRow);
    layout->addWidget(group);

    connect(m_annotations, &QCheckBox::toggled, this, &BurnInOptionsWidget::refreshEnabled);
    connect(m_annotationNotes, &QCheckBox::toggled, this, &BurnInOptionsWidget::refreshEnabled);
    connect(m_afterNote, &QComboBox::currentIndexChanged, this, &BurnInOptionsWidget::refreshEnabled);
    refreshEnabled();
}

void BurnInOptionsWidget::refreshEnabled()
{
    const bool any = m_annotations->isChecked() || m_annotationNotes->isChecked();
    m_afterNote->setEnabled(any);
    m_holdFrames->setEnabled(any && AfterNote(m_afterNote->currentData().toInt()) != AfterNote::PlayThrough);
}

void BurnInOptionsWidget::setVideoOptionsVisible(bool visible) { m_afterRow->setVisible(visible); }

exporter::ExportBurnIns BurnInOptionsWidget::options() const
{
    exporter::ExportBurnIns options;
    options.frameNumber = m_frameNumber->isChecked();
    options.bookmarkLabels = m_bookmarkLabels->isChecked();
    options.bookmarkNotes = m_bookmarkNotes->isChecked();
    options.annotations = m_annotations->isChecked();
    options.annotationNotes = m_annotationNotes->isChecked();
    const auto after = AfterNote(m_afterNote->currentData().toInt());
    if (after == AfterNote::KeepOnScreen) options.annotationHoldFrames = m_holdFrames->value();
    if (after == AfterNote::Freeze) options.annotationFreezeFrames = m_holdFrames->value();
    options.tracingPaperPercent = m_tracingPaper->currentData().toInt();
    return options;
}

void BurnInOptionsWidget::setOptions(const exporter::ExportBurnIns& options)
{
    m_frameNumber->setChecked(options.frameNumber);
    m_bookmarkLabels->setChecked(options.bookmarkLabels);
    m_bookmarkNotes->setChecked(options.bookmarkNotes);
    m_annotations->setChecked(options.annotations);
    m_annotationNotes->setChecked(options.annotationNotes);
    AfterNote after = AfterNote::PlayThrough;
    if (options.annotationFreezeFrames > 0) {
        after = AfterNote::Freeze;
        m_holdFrames->setValue(options.annotationFreezeFrames);
    } else if (options.annotationHoldFrames > 0) {
        after = AfterNote::KeepOnScreen;
        m_holdFrames->setValue(options.annotationHoldFrames);
    }
    m_afterNote->setCurrentIndex(m_afterNote->findData(int(after)));
    // The nearest level the menu offers.
    int best = 0;
    for (int i = 0; i < m_tracingPaper->count(); ++i)
        if (std::abs(m_tracingPaper->itemData(i).toInt() - options.tracingPaperPercent)
            < std::abs(m_tracingPaper->itemData(best).toInt() - options.tracingPaperPercent))
            best = i;
    m_tracingPaper->setCurrentIndex(best);
    refreshEnabled();
}

} // namespace atk::ui
