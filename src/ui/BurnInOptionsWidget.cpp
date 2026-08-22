#include "ui/BurnInOptionsWidget.h"
#include <QCheckBox>
#include <QGroupBox>
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
    layout->addWidget(group);
}

exporter::ExportBurnIns BurnInOptionsWidget::options() const
{
    return {m_frameNumber->isChecked(), m_bookmarkLabels->isChecked(), m_bookmarkNotes->isChecked()};
}

void BurnInOptionsWidget::setOptions(const exporter::ExportBurnIns& options)
{
    m_frameNumber->setChecked(options.frameNumber);
    m_bookmarkLabels->setChecked(options.bookmarkLabels);
    m_bookmarkNotes->setChecked(options.bookmarkNotes);
}

} // namespace atk::ui
