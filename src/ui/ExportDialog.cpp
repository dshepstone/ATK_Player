#include "ui/ExportDialog.h"
#include "ui/BurnInOptionsWidget.h"
#include "ui/ExportUi.h"
#include "export/FFmpegExporter.h"
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
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

    // The project's save location: set once, and every export of this
    // project starts there instead of beside the project. Browse still
    // picks any file for this export alone.
    auto* remembered = new QWidget(this);
    remembered->setObjectName(QStringLiteral("ExportProjectFolderRow"));
    auto* rememberedRow = new QHBoxLayout(remembered);
    rememberedRow->setContentsMargins(0, 0, 0, 0);
    m_projectFolderLabel = new QLabel(remembered);
    m_projectFolderLabel->setObjectName(QStringLiteral("ExportProjectFolder"));
    m_projectFolderLabel->setWordWrap(true);
    m_projectFolderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* setFolder = new QPushButton(tr("Set Save Location…"), remembered);
    setFolder->setObjectName(QStringLiteral("ExportSetProjectFolder"));
    setFolder->setToolTip(tr("Choose a folder for this project's exports. It is remembered in the "
                             "project, so you do not have to browse to it each time."));
    m_clearProjectFolder = new QPushButton(tr("Clear"), remembered);
    m_clearProjectFolder->setObjectName(QStringLiteral("ExportClearProjectFolder"));
    m_clearProjectFolder->setToolTip(tr("Forget this project's save location; exports go beside the project again"));
    rememberedRow->addWidget(m_projectFolderLabel, 1);
    rememberedRow->addWidget(setFolder);
    rememberedRow->addWidget(m_clearProjectFolder);
    exportui::addRow(output, this, tr("Project folder"), remembered);
    connect(setFolder, &QPushButton::clicked, this, &ExportDialog::chooseProjectFolder);
    connect(m_clearProjectFolder, &QPushButton::clicked, this, &ExportDialog::clearProjectFolder);
    showProjectFolder();
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

    // The review keeps these choices, so its next export (and the next time
    // it is opened) starts from them instead of the defaults.
    auto* keep = new QWidget(this);
    keep->setObjectName(QStringLiteral("ExportRow"));
    auto* keepRow = new QHBoxLayout(keep);
    keepRow->setContentsMargins(0, 6, 0, 0);
    m_rememberBurnIns = new QCheckBox(tr("Remember these choices for this review"), keep);
    m_rememberBurnIns->setObjectName(QStringLiteral("ExportRememberBurnIns"));
    m_rememberBurnIns->setChecked(true);
    m_rememberBurnIns->setToolTip(tr("Every export of this review or playlist starts from these choices, until "
                                     "another review or clip is opened. Saved with the project."));
    m_defaultBurnIns = new QPushButton(tr("Use Defaults"), keep);
    m_defaultBurnIns->setObjectName(QStringLiteral("ExportBurnInDefaults"));
    m_defaultBurnIns->setToolTip(tr("Use the standard choices for this export only; the review keeps its saved ones"));
    m_defaultBurnIns->setVisible(false);
    keepRow->addWidget(m_rememberBurnIns);
    keepRow->addStretch(1);
    keepRow->addWidget(m_defaultBurnIns);
    burnIns->addWidget(keep);
    m_savedBurnInsNote = new QLabel(tr("Using the choices saved with this review."), this);
    m_savedBurnInsNote->setObjectName(QStringLiteral("ExportNote"));
    m_savedBurnInsNote->setVisible(false);
    burnIns->addWidget(m_savedBurnInsNote);
    connect(m_defaultBurnIns, &QPushButton::clicked, this, [this] {
        m_burnIns->setOptions(m_spec.burnIns);
        m_defaultsForThisExport = true;
        m_savedBurnInsNote->setText(tr("Standard choices for this export only; the review keeps its saved ones."));
        m_defaultBurnIns->setVisible(false);
    });

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
void ExportDialog::setSavedBurnIns(const std::optional<exporter::ExportBurnIns>& saved)
{
    m_burnIns->setOptions(saved ? *saved : m_spec.burnIns);
    m_defaultsForThisExport = false;
    m_savedBurnInsNote->setText(tr("Using the choices saved with this review."));
    m_savedBurnInsNote->setVisible(saved.has_value());
    m_defaultBurnIns->setVisible(saved.has_value());
}
bool ExportDialog::rememberBurnIns() const { return m_rememberBurnIns->isChecked(); }
void ExportDialog::setRememberBurnIns(bool remember) { m_rememberBurnIns->setChecked(remember); }
void ExportDialog::setOpenWhenDone(bool open) { m_openWhenDone->setChecked(open); }
bool ExportDialog::openWhenDone() const { return m_openWhenDone->isChecked(); }
void ExportDialog::setProjectFolder(const QString& folder, bool projectSaved)
{
    m_projectFolder = folder;
    m_projectSaved = projectSaved;
    showProjectFolder();
}

void ExportDialog::showProjectFolder()
{
    QString text;
    if (m_projectFolder.isEmpty())
        text = tr("Not set: exports go beside the project");
    else if (!QFileInfo(m_projectFolder).isDir())
        text = tr("%1 (not found)").arg(QDir::toNativeSeparators(m_projectFolder));
    else
        text = QDir::toNativeSeparators(m_projectFolder);
    if (!m_projectFolder.isEmpty() && !m_projectSaved) text += tr("\nKept once the project is saved.");
    m_projectFolderLabel->setText(text);
    m_projectFolderLabel->setToolTip(QDir::toNativeSeparators(m_projectFolder));
    m_clearProjectFolder->setVisible(!m_projectFolder.isEmpty());
}

void ExportDialog::chooseProjectFolder()
{
    const QString start = QFileInfo(m_projectFolder).isDir() ? m_projectFolder
                                                             : QFileInfo(destination()).absolutePath();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Set Save Location"), start);
    if (folder.isEmpty()) return;
    m_projectFolder = QDir::cleanPath(folder);
    // This export goes there too, under the name it already has.
    m_destination->setText(QDir(m_projectFolder).filePath(QFileInfo(destination()).fileName()));
    showProjectFolder();
    emit projectFolderChanged(m_projectFolder);
}

void ExportDialog::clearProjectFolder()
{
    m_projectFolder.clear();
    showProjectFolder();
    emit projectFolderChanged(QString());
}

void ExportDialog::browse()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Review"), destination(), tr("MP4 Video (*.mp4)"));
    if (!path.isEmpty()) m_destination->setText(path);
}

} // namespace atk::ui
