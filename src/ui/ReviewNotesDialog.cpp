#include "ui/ReviewNotesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace atk::ui {

ReviewNotesDialog::ReviewNotesDialog(const annotation::SourceAnnotations& annotations,
                                     const QString& suggestedPath, QWidget* parent)
    : QDialog(parent), m_annotations(annotations)
{
    setObjectName(QStringLiteral("ReviewNotesDialog"));
    setWindowTitle(tr("Export Notes PDF"));
    auto* root = new QVBoxLayout(this);
    auto* title = new QLabel(tr("EXPORT REVIEW NOTES"), this);
    title->setProperty("atkRole", "heading");
    root->addWidget(title);
    auto* intro = new QLabel(tr("A PDF with one row per frame that has drawings or notes: the frame with its "
                                "drawings, its frame number and timecode, and the notes written on it."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* form = new QFormLayout;
    auto* row = new QHBoxLayout;
    m_destination = new QLineEdit(QDir::toNativeSeparators(suggestedPath), this);
    m_destination->setObjectName(QStringLiteral("ReviewNotesDestination"));
    auto* browseButton = new QPushButton(tr("Browse…"), this);
    row->addWidget(m_destination, 1);
    row->addWidget(browseButton);
    form->addRow(tr("Destination:"), row);

    m_paper = new QComboBox(this);
    m_paper->setObjectName(QStringLiteral("ReviewNotesPaper"));
    m_paper->addItem(tr("A4"), int(QPageSize::A4));
    m_paper->addItem(tr("US Letter"), int(QPageSize::Letter));
    // Letter where it is the local standard, A4 elsewhere.
    const QLocale::Territory territory = QLocale().territory();
    if (territory == QLocale::UnitedStates || territory == QLocale::Canada || territory == QLocale::Mexico)
        m_paper->setCurrentIndex(1);
    form->addRow(tr("Paper:"), m_paper);

    m_includeResolved = new QCheckBox(tr("Include resolved notes"), this);
    m_includeResolved->setObjectName(QStringLiteral("ReviewNotesIncludeResolved"));
    m_includeResolved->setChecked(true);
    form->addRow(QString(), m_includeResolved);
    root->addLayout(form);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("ReviewNotesSummary"));
    root->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_export = buttons->addButton(tr("Export PDF"), QDialogButtonBox::AcceptRole);
    m_export->setObjectName(QStringLiteral("ReviewNotesExport"));
    root->addWidget(buttons);
    connect(browseButton, &QPushButton::clicked, this, &ReviewNotesDialog::browse);
    connect(m_includeResolved, &QCheckBox::toggled, this, &ReviewNotesDialog::refreshSummary);
    connect(m_destination, &QLineEdit::textChanged, this, &ReviewNotesDialog::refreshSummary);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
    return options;
}

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
