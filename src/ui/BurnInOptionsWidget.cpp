#include "ui/BurnInOptionsWidget.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

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
    m_annotationNotes = new QCheckBox(tr("Frame notes"), group);
    choices->addWidget(m_annotations);
    choices->addWidget(m_annotationNotes);
    auto* hold = new QHBoxLayout;
    hold->addSpacing(20);
    hold->addWidget(new QLabel(tr("Keep on screen for"), group));
    m_holdFrames = new QSpinBox(group);
    m_holdFrames->setRange(0, 240);
    m_holdFrames->setSuffix(tr(" more frames"));
    m_holdFrames->setToolTip(tr("0 shows drawings and notes only on their own frame, so you step to them; "
                                "a hold keeps them readable while the video plays through"));
    hold->addWidget(m_holdFrames);
    hold->addStretch(1);
    choices->addLayout(hold);
    layout->addWidget(group);
}

exporter::ExportBurnIns BurnInOptionsWidget::options() const
{
    exporter::ExportBurnIns options;
    options.frameNumber = m_frameNumber->isChecked();
    options.bookmarkLabels = m_bookmarkLabels->isChecked();
    options.bookmarkNotes = m_bookmarkNotes->isChecked();
    options.annotations = m_annotations->isChecked();
    options.annotationNotes = m_annotationNotes->isChecked();
    options.annotationHoldFrames = m_holdFrames->value();
    return options;
}

void BurnInOptionsWidget::setOptions(const exporter::ExportBurnIns& options)
{
    m_frameNumber->setChecked(options.frameNumber);
    m_bookmarkLabels->setChecked(options.bookmarkLabels);
    m_bookmarkNotes->setChecked(options.bookmarkNotes);
    m_annotations->setChecked(options.annotations);
    m_annotationNotes->setChecked(options.annotationNotes);
    m_holdFrames->setValue(options.annotationHoldFrames);
}

} // namespace atk::ui
