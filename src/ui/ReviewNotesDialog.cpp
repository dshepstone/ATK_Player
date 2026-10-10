#include "ui/ReviewNotesDialog.h"

#include "ui/ExportUi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdlib>

namespace atk::ui {

ReviewNotesDialog::ReviewNotesDialog(const annotation::SourceAnnotations& annotations,
                                     const QString& suggestedPath, int tracingPaperPercent, QWidget* parent)
    : QDialog(parent), m_annotations(annotations)
{
    setObjectName(QStringLiteral("ReviewNotesDialog"));
    setWindowTitle(tr("Export Notes PDF"));
    setStyleSheet(exportui::styleSheet());
    setMinimumWidth(540);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_summary = exportui::addHeader(root, this, QStringLiteral("PDF"), tr("Export Notes PDF"), QString());
    m_summary->setObjectName(QStringLiteral("ReviewNotesSummary"));
    auto* body = exportui::addBody(root);

    auto* output = exportui::addCard(body, this, tr("Output"));
    auto* where = new QWidget(this);
    where->setObjectName(QStringLiteral("ExportRow"));
    auto* whereRow = new QHBoxLayout(where);
    whereRow->setContentsMargins(0, 0, 0, 0);
    m_destination = new QLineEdit(QDir::toNativeSeparators(suggestedPath), where);
    m_destination->setObjectName(QStringLiteral("ReviewNotesDestination"));
    auto* browseButton = new QPushButton(tr("Browse…"), where);
    whereRow->addWidget(m_destination, 1);
    whereRow->addWidget(browseButton);
    exportui::addRow(output, this, tr("Save to"), where);
    m_paper = new QComboBox(this);
    m_paper->setObjectName(QStringLiteral("ReviewNotesPaper"));
    m_paper->addItem(tr("A4"), int(QPageSize::A4));
    m_paper->addItem(tr("US Letter"), int(QPageSize::Letter));
    // Letter where it is the local standard, A4 elsewhere.
    const QLocale::Territory territory = QLocale().territory();
    if (territory == QLocale::UnitedStates || territory == QLocale::Canada || territory == QLocale::Mexico)
        m_paper->setCurrentIndex(1);
    exportui::addRow(output, this, tr("Paper"), m_paper, 0);

    auto* content = exportui::addCard(body, this, tr("Content"));
    m_includeResolved = new QCheckBox(tr("Include resolved notes"), this);
    m_includeResolved->setObjectName(QStringLiteral("ReviewNotesIncludeResolved"));
    m_includeResolved->setChecked(true);
    content->addWidget(m_includeResolved);
    m_tracingPaper = new QComboBox(this);
    m_tracingPaper->setObjectName(QStringLiteral("ReviewNotesTracingPaper"));
    for (int level : {0, 25, 50, 75, 100})
        m_tracingPaper->addItem(level == 0 ? tr("Off") : QStringLiteral("%1%").arg(level), level);
    m_tracingPaper->setToolTip(tr("Fade the pictures under the drawings, as tracing paper does in the viewer"));
    int nearest = 0;
    for (int i = 0; i < m_tracingPaper->count(); ++i)
        if (std::abs(m_tracingPaper->itemData(i).toInt() - tracingPaperPercent)
            < std::abs(m_tracingPaper->itemData(nearest).toInt() - tracingPaperPercent))
            nearest = i;
    m_tracingPaper->setCurrentIndex(nearest);
    exportui::addRow(content, this, tr("Tracing paper"), m_tracingPaper, 0);

    m_openWhenDone = new QCheckBox(tr("Open the PDF when it is done"), this);
    m_openWhenDone->setObjectName(QStringLiteral("ReviewNotesOpenWhenDone"));
    m_openWhenDone->setChecked(true);
    m_export = exportui::addFooter(root, this, tr("Export PDF"), m_openWhenDone);
    m_export->setObjectName(QStringLiteral("ReviewNotesExport"));
    connect(browseButton, &QPushButton::clicked, this, &ReviewNotesDialog::browse);
    connect(m_includeResolved, &QCheckBox::toggled, this, &ReviewNotesDialog::refreshSummary);
    connect(m_destination, &QLineEdit::textChanged, this, &ReviewNotesDialog::refreshSummary);
    refreshSummary();
}

QString ReviewNotesDialog::destination() const
{
    QString path = QDir::fromNativeSeparators(m_destination->text().trimmed());
    if (path.isEmpty()) return {};
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
    return QFileInfo(path).absoluteFilePath();
}

exporter::ReviewNotesOptions ReviewNotesDialog::options() const
{
    exporter::ReviewNotesOptions options;
    options.outputPath = destination();
    options.pageSize = QPageSize(QPageSize::PageSizeId(m_paper->currentData().toInt()));
    options.includeResolved = m_includeResolved->isChecked();
    options.tracingPaperPercent = m_tracingPaper->currentData().toInt();
    return options;
}

void ReviewNotesDialog::setOpenWhenDone(bool open) { m_openWhenDone->setChecked(open); }
bool ReviewNotesDialog::openWhenDone() const { return m_openWhenDone->isChecked(); }

void ReviewNotesDialog::browse()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export Review Notes"), destination(),
                                                tr("PDF Document (*.pdf)"));
    if (!path.isEmpty()) m_destination->setText(QDir::toNativeSeparators(path));
}

void ReviewNotesDialog::refreshSummary()
{
    const qsizetype frames =
        exporter::ReviewNotesJob::framesToInclude(m_annotations, m_includeResolved->isChecked()).size();
    m_summary->setText(frames == 0 ? tr("No frames have drawings or notes yet.")
                                   : tr("%n frame(s) will be listed.", nullptr, int(frames)));
    m_export->setEnabled(frames > 0 && !destination().isEmpty());
}

} // namespace atk::ui
