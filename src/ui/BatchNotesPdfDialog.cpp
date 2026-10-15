#include "ui/BatchNotesPdfDialog.h"

#include "ui/ExportUi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdlib>

namespace atk::ui {

BatchNotesPdfDialog::BatchNotesPdfDialog(QVector<BatchNotesClip> clips, const QString& folder,
                                         int tracingPaperPercent, QWidget* parent)
    : QDialog(parent), m_clips(std::move(clips))
{
    setObjectName(QStringLiteral("BatchNotesPdfDialog"));
    setWindowTitle(tr("Export Notes PDFs for Playlist"));
    setStyleSheet(exportui::styleSheet());
    setMinimumWidth(560);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_summary = exportui::addHeader(root, this, QStringLiteral("PDF"), tr("Export Notes PDFs for Playlist"), QString());
    m_summary->setObjectName(QStringLiteral("BatchNotesSummary"));
    auto* body = exportui::addBody(root);

    auto* clipsCard = exportui::addCard(body, this, tr("Clips (one PDF each)"));
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("BatchNotesClips"));
    m_list->setMinimumHeight(180);
    for (int i = 0; i < m_clips.size(); ++i) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, i);
        item->setCheckState(Qt::Unchecked);
    }
    clipsCard->addWidget(m_list);
    auto* selectRow = new QHBoxLayout;
    auto* selectAll = new QPushButton(tr("Select All"), this);
    selectAll->setObjectName(QStringLiteral("BatchNotesSelectAll"));
    auto* selectNone = new QPushButton(tr("Select None"), this);
    selectNone->setObjectName(QStringLiteral("BatchNotesSelectNone"));
    selectRow->addWidget(selectAll);
    selectRow->addWidget(selectNone);
    selectRow->addStretch(1);
    clipsCard->addLayout(selectRow);

    auto* output = exportui::addCard(body, this, tr("Output"));
    auto* where = new QWidget(this);
    where->setObjectName(QStringLiteral("ExportRow"));
    auto* whereRow = new QHBoxLayout(where);
    whereRow->setContentsMargins(0, 0, 0, 0);
    m_folder = new QLineEdit(QDir::toNativeSeparators(folder), where);
    m_folder->setObjectName(QStringLiteral("BatchNotesFolder"));
    auto* browseButton = new QPushButton(tr("Browse…"), where);
    whereRow->addWidget(m_folder, 1);
    whereRow->addWidget(browseButton);
    exportui::addRow(output, this, tr("Folder"), where);
    m_paper = new QComboBox(this);
    m_paper->setObjectName(QStringLiteral("BatchNotesPaper"));
    m_paper->addItem(tr("A4"), int(QPageSize::A4));
    m_paper->addItem(tr("US Letter"), int(QPageSize::Letter));
    // Letter where it is the local standard, A4 elsewhere (as Export Notes PDF).
    const QLocale::Territory territory = QLocale().territory();
    if (territory == QLocale::UnitedStates || territory == QLocale::Canada || territory == QLocale::Mexico)
        m_paper->setCurrentIndex(1);
    exportui::addRow(output, this, tr("Paper"), m_paper, 0);

    auto* content = exportui::addCard(body, this, tr("Content"));
    m_includeNoteText = new QCheckBox(tr("Include text notes"), this);
    m_includeNoteText->setObjectName(QStringLiteral("BatchNotesIncludeText"));
    m_includeNoteText->setToolTip(tr("Off: only the pictures with their drawings, each captioned with its frame "
                                     "and timecode. Frames with notes but no drawings are left out."));
    m_includeNoteText->setChecked(true);
    content->addWidget(m_includeNoteText);
    m_includeResolved = new QCheckBox(tr("Include resolved notes"), this);
    m_includeResolved->setObjectName(QStringLiteral("BatchNotesIncludeResolved"));
    m_includeResolved->setChecked(true);
    content->addWidget(m_includeResolved);
    m_tracingPaper = new QComboBox(this);
    m_tracingPaper->setObjectName(QStringLiteral("BatchNotesTracingPaper"));
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

    m_openWhenDone = new QCheckBox(tr("Open the folder when it is done"), this);
    m_openWhenDone->setObjectName(QStringLiteral("BatchNotesOpenWhenDone"));
    m_openWhenDone->setChecked(true);
    m_export = exportui::addFooter(root, this, tr("Export PDFs"), m_openWhenDone);
    m_export->setObjectName(QStringLiteral("BatchNotesExport"));

    connect(browseButton, &QPushButton::clicked, this, &BatchNotesPdfDialog::browse);
    connect(selectAll, &QPushButton::clicked, this, [this] {
        m_refreshing = true;
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->flags() & Qt::ItemIsEnabled) m_list->item(i)->setCheckState(Qt::Checked);
        m_refreshing = false;
        refresh();
    });
    connect(selectNone, &QPushButton::clicked, this, [this] {
        m_refreshing = true;
        for (int i = 0; i < m_list->count(); ++i) m_list->item(i)->setCheckState(Qt::Unchecked);
        m_refreshing = false;
        refresh();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this] { if (!m_refreshing) refresh(); });
    connect(m_includeResolved, &QCheckBox::toggled, this, &BatchNotesPdfDialog::refresh);
    connect(m_includeNoteText, &QCheckBox::toggled, this, [this](bool on) {
        m_includeResolved->setEnabled(on); // no text, nothing resolved to show
        refresh();
    });
    connect(m_folder, &QLineEdit::textChanged, this, &BatchNotesPdfDialog::refresh);

    // Every clip with something to list starts ticked.
    m_refreshing = true;
    for (int i = 0; i < m_list->count(); ++i) {
        const BatchNotesClip& clip = m_clips.at(i);
        if (clip.unavailableReason.isEmpty() && framesFor(clip) > 0) m_list->item(i)->setCheckState(Qt::Checked);
    }
    m_refreshing = false;
    refresh();
}

int BatchNotesPdfDialog::framesFor(const BatchNotesClip& clip) const
{
    return int(exporter::ReviewNotesJob::framesToInclude(clip.annotations, m_includeResolved->isChecked(),
                                                         m_includeNoteText->isChecked())
                   .size());
}

QVector<QUuid> BatchNotesPdfDialog::chosenClips() const
{
    QVector<QUuid> chosen;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem* item = m_list->item(i);
        if ((item->flags() & Qt::ItemIsEnabled) && item->checkState() == Qt::Checked)
            chosen.append(m_clips.at(item->data(Qt::UserRole).toInt()).id);
    }
    return chosen;
}

QString BatchNotesPdfDialog::folder() const
{
    const QString path = QDir::fromNativeSeparators(m_folder->text().trimmed());
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

exporter::ReviewNotesOptions BatchNotesPdfDialog::options() const
{
    exporter::ReviewNotesOptions options;
    options.pageSize = QPageSize(QPageSize::PageSizeId(m_paper->currentData().toInt()));
    options.includeNoteText = m_includeNoteText->isChecked();
    options.includeResolved = m_includeResolved->isChecked();
    options.tracingPaperPercent = m_tracingPaper->currentData().toInt();
    return options;
}

void BatchNotesPdfDialog::setOpenFolderWhenDone(bool open) { m_openWhenDone->setChecked(open); }
bool BatchNotesPdfDialog::openFolderWhenDone() const { return m_openWhenDone->isChecked(); }
void BatchNotesPdfDialog::setIncludeNoteText(bool include) { m_includeNoteText->setChecked(include); }
bool BatchNotesPdfDialog::includeNoteText() const { return m_includeNoteText->isChecked(); }

void BatchNotesPdfDialog::browse()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Export Notes PDFs to"), folder());
    if (!path.isEmpty()) m_folder->setText(QDir::toNativeSeparators(path));
}

void BatchNotesPdfDialog::refresh()
{
    // Counts follow the content choices; a clip left with nothing to list
    // cannot be chosen.
    m_refreshing = true;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        const BatchNotesClip& clip = m_clips.at(item->data(Qt::UserRole).toInt());
        const int frames = framesFor(clip);
        const bool usable = clip.unavailableReason.isEmpty() && frames > 0;
        const QString detail = !clip.unavailableReason.isEmpty() ? clip.unavailableReason
                             : frames == 0 ? tr("nothing to list")
                                           : tr("%n frame(s)", nullptr, frames);
        item->setText(QStringLiteral("%1. %2 — %3").arg(i + 1).arg(clip.name, detail));
        Qt::ItemFlags flags = item->flags();
        flags.setFlag(Qt::ItemIsEnabled, usable);
        flags.setFlag(Qt::ItemIsUserCheckable, usable);
        item->setFlags(flags);
        if (!usable) item->setCheckState(Qt::Unchecked);
    }
    m_refreshing = false;

    const int chosen = int(chosenClips().size());
    m_summary->setText(chosen == 0 ? tr("Choose the clips to export.")
                                   : tr("%n PDF(s) will be saved, one per clip.", nullptr, chosen));
    m_export->setEnabled(chosen > 0 && !folder().isEmpty());
}

} // namespace atk::ui
