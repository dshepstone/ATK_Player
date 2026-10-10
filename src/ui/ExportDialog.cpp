#include "ui/ExportDialog.h"
#include "ui/BurnInOptionsWidget.h"
#include "ui/ExportUi.h"
#include "export/FFmpegExporter.h"
#include <QCheckBox>
#include <QFileDialog>
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
    setObjectName(QStringLiteral("ExportReviewDialog"));
    setWindowTitle(tr("Export Review"));
    setStyleSheet(exportui::styleSheet());
    setMinimumWidth(560);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    const QString mode = m_spec.comparison ? modeName(m_spec.layout) : tr("Single source");
    exportui::addHeader(root, this, QStringLiteral("MP4"), tr("Export Review Video"),
        tr("Frames %1–%2 (%3 frames)  ·  %4 × %5  ·  %6")
            .arg(m_spec.sourceA.rangeStartFrame + 1).arg(m_spec.sourceA.rangeEndFrame + 1).arg(m_spec.frameCount())
            .arg(m_spec.outputSize().width()).arg(m_spec.outputSize().height()).arg(mode));
    auto* body = exportui::addBody(root);

    auto* output = exportui::addCard(body, this, tr("Output"));
    auto* where = new QWidget(this);
    where->setObjectName(QStringLiteral("ExportRow"));
    auto* whereRow = new QHBoxLayout(where);
    whereRow->setContentsMargins(0, 0, 0, 0);
    m_destination = new QLineEdit(m_spec.outputPath, where);
    m_destination->setObjectName(QStringLiteral("ExportDestination"));
    auto* browseButton = new QPushButton(tr("Browse…"), where);
    browseButton->setObjectName(QStringLiteral("ExportBrowse"));
    whereRow->addWidget(m_destination, 1);
    whereRow->addWidget(browseButton);
    exportui::addRow(output, this, tr("Save to"), where);
    exportui::addRow(output, this, tr("Format"), new QLabel(tr("MP4 · H.264 (%1)").arg(m_spec.videoEncoder), this));
    exportui::addRow(output, this, tr("Sound"), new QLabel(m_spec.audioSummary(), this));
    if (!exporter::FFmpegExporter::isSupported()) {
        auto* warning = new QLabel(tr("No compatible H.264 encoder is available in this FFmpeg build."), this);
        warning->setObjectName(QStringLiteral("ExportWarning"));
        output->addWidget(warning);
    }

    auto* burnIns = exportui::addCard(body, this, tr("Drawings, notes and burn-ins"));
    m_burnIns = new BurnInOptionsWidget(this);
    m_burnIns->setOptions(m_spec.burnIns);
    burnIns->addWidget(m_burnIns);

    m_openWhenDone = new QCheckBox(tr("Open the video when it is done"), this);
    m_openWhenDone->setObjectName(QStringLiteral("ExportOpenWhenDone"));
    m_openWhenDone->setChecked(true);
    m_export = exportui::addFooter(root, this, tr("Export Video"), m_openWhenDone);
    m_export->setObjectName(QStringLiteral("ExportStart"));
    m_export->setEnabled(exporter::FFmpegExporter::isSupported());
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
}

QString ExportDialog::destination() const
{
    QString path = m_destination->text().trimmed();
    if (!path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) path += QStringLiteral(".mp4");
    return path;
}
exporter::ExportBurnIns ExportDialog::burnIns() const { return m_burnIns->options(); }
void ExportDialog::setOpenWhenDone(bool open) { m_openWhenDone->setChecked(open); }
bool ExportDialog::openWhenDone() const { return m_openWhenDone->isChecked(); }
void ExportDialog::browse()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Review"), destination(), tr("MP4 Video (*.mp4)"));
    if (!path.isEmpty()) m_destination->setText(path);
}

} // namespace atk::ui
