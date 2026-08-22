#include "ui/ExportDialog.h"
#include "export/FFmpegExporter.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace atk::ui {
namespace {
QString modeName(playback::CompareLayout mode)
{
    switch (mode) {
    case playback::CompareLayout::SideBySide: return QStringLiteral("Side by Side");
    case playback::CompareLayout::Stacked: return QStringLiteral("Stacked");
    case playback::CompareLayout::Wipe: return QStringLiteral("Wipe");
    case playback::CompareLayout::Blend: return QStringLiteral("Blend");
    case playback::CompareLayout::Difference: return QStringLiteral("Difference");
    }
    return {};
}
}

ExportDialog::ExportDialog(exporter::ExportSpec spec, QWidget* parent)
    : QDialog(parent), m_spec(std::move(spec))
{
    setObjectName(QStringLiteral("ExportReviewDialog")); setWindowTitle(tr("Export Review"));
    auto* root = new QVBoxLayout(this);
    auto* title = new QLabel(tr("EXPORT REVIEW"), this); title->setProperty("atkRole", "heading"); root->addWidget(title);
    auto* form = new QFormLayout;
    auto* row = new QHBoxLayout;
    m_destination = new QLineEdit(m_spec.outputPath, this); m_destination->setObjectName(QStringLiteral("ExportDestination"));
    auto* browseButton = new QPushButton(tr("Browse…"), this); browseButton->setObjectName(QStringLiteral("ExportBrowse"));
    row->addWidget(m_destination, 1); row->addWidget(browseButton); form->addRow(tr("Destination:"), row);
    form->addRow(tr("Range:"), new QLabel(tr("Active Review Range — Frames %1–%2 inclusive (%3 frames)")
        .arg(m_spec.sourceA.rangeStartFrame + 1).arg(m_spec.sourceA.rangeEndFrame + 1).arg(m_spec.frameCount()), this));
    form->addRow(tr("Video:"), new QLabel(tr("MP4 / H.264 (%1)").arg(m_spec.videoEncoder), this));
    const QString mode = m_spec.comparison ? modeName(m_spec.layout) : tr("Single Source");
    form->addRow(tr("Canvas:"), new QLabel(tr("%1 × %2 — %3 (Fit, no viewer crop)")
        .arg(m_spec.outputSize().width()).arg(m_spec.outputSize().height()).arg(mode), this));
    form->addRow(tr("Audio:"), new QLabel(m_spec.audioSummary(), this));
    root->addLayout(form);
    if (!exporter::FFmpegExporter::isSupported())
        root->addWidget(new QLabel(tr("No compatible H.264 encoder is available in this FFmpeg build."), this));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_export = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    m_export->setObjectName(QStringLiteral("ExportStart")); m_export->setEnabled(exporter::FFmpegExporter::isSupported());
    root->addWidget(buttons);
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString ExportDialog::destination() const
{
    QString path = m_destination->text().trimmed();
    if (!path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) path += QStringLiteral(".mp4");
    return path;
}
void ExportDialog::browse()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Review"), destination(), tr("MP4 Video (*.mp4)"));
    if (!path.isEmpty()) m_destination->setText(path);
}

} // namespace atk::ui
