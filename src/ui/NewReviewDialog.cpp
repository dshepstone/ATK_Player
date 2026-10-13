#include "ui/NewReviewDialog.h"

#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

namespace atk::ui {
namespace {

constexpr int kClipIndexRole = Qt::UserRole + 1;

/// Characters no file name may contain on any of Windows, macOS or Linux.
bool isUsableFileName(const QString& name)
{
    static const QRegularExpression illegal(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    return !name.trimmed().isEmpty() && !name.contains(illegal) && !name.endsWith(QLatin1Char('.'))
        && !name.endsWith(QLatin1Char(' '));
}

} // namespace

NewReviewDialog::NewReviewDialog(const QString& startFolder, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("NewReviewDialog"));
    setWindowTitle(tr("New Review"));
    setMinimumSize(560, 560);
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto* intro = new QLabel(tr("Choose a folder of clips. Each clip you tick becomes an item in the review, in the "
                                "order shown; drag to change it. Notes and drawings are saved in a project file "
                                "in that folder."),
                             this);
    intro->setWordWrap(true);
    intro->setProperty("atkRole", "placeholder");
    root->addWidget(intro);

    auto* folderRow = new QHBoxLayout;
    m_folder = new QLineEdit(this);
    m_folder->setObjectName(QStringLiteral("NewReviewFolder"));
    m_folder->setAccessibleName(tr("Folder"));
    m_folder->setPlaceholderText(tr("Folder of clips"));
    auto* browse = new QPushButton(icons::icon(QStringLiteral("folder-open")), tr("Browse…"), this);
    browse->setObjectName(QStringLiteral("NewReviewBrowse"));
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);
    root->addLayout(folderRow);
    m_subfolders = new QCheckBox(tr("Include clips in subfolders"), this);
    m_subfolders->setObjectName(QStringLiteral("NewReviewSubfolders"));
    root->addWidget(m_subfolders);
    connect(browse, &QPushButton::clicked, this, &NewReviewDialog::browse);
    connect(m_folder, &QLineEdit::editingFinished, this, &NewReviewDialog::startScan);
    connect(m_subfolders, &QCheckBox::toggled, this, &NewReviewDialog::startScan);

    auto* listTools = new QHBoxLayout;
    auto* all = new QPushButton(tr("Select All"), this);
    auto* none = new QPushButton(tr("Select None"), this);
    m_order = new QComboBox(this);
    m_order->setObjectName(QStringLiteral("NewReviewOrder"));
    m_order->addItem(tr("Order by name"), int(Order::Name));
    m_order->addItem(tr("Order by date modified"), int(Order::Modified));
    listTools->addWidget(all);
    listTools->addWidget(none);
    listTools->addStretch(1);
    listTools->addWidget(m_order);
    root->addLayout(listTools);

    m_clips = new QListWidget(this);
    m_clips->setObjectName(QStringLiteral("NewReviewClips"));
    m_clips->setAccessibleName(tr("Clips to include"));
    m_clips->setDragDropMode(QAbstractItemView::InternalMove);
    m_clips->setDefaultDropAction(Qt::MoveAction);
    m_clips->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root->addWidget(m_clips, 1);
    connect(all, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_clips->count(); ++i) m_clips->item(i)->setCheckState(Qt::Checked);
    });
    connect(none, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_clips->count(); ++i) m_clips->item(i)->setCheckState(Qt::Unchecked);
    });
    connect(m_order, &QComboBox::currentIndexChanged, this, &NewReviewDialog::applyOrder);
    connect(m_clips, &QListWidget::itemChanged, this, &NewReviewDialog::refreshState);

    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("Review name"), this));
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("NewReviewName"));
    m_name->setAccessibleName(tr("Review name"));
    nameRow->addWidget(m_name, 1);
    root->addLayout(nameRow);
    connect(m_name, &QLineEdit::textEdited, this, [this] {
        m_nameEdited = true;
        refreshState();
    });

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("NewReviewSummary"));
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_create = buttons->addButton(tr("Create Review"), QDialogButtonBox::AcceptRole);
    m_create->setObjectName(QStringLiteral("NewReviewCreate"));
    m_create->setProperty("atkRole", "primary");
    m_create->style()->unpolish(m_create); // the box polished it before the role was set
    m_create->style()->polish(m_create);
    m_create->setDefault(true);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshState();
    if (!startFolder.isEmpty()) setFolder(startFolder);
}

NewReviewDialog::~NewReviewDialog()
{
    // Closing moves focus out of the folder field; that must not start a scan
    // on a dialog that is going away.
    m_folder->disconnect(this);
    m_subfolders->disconnect(this);
    cancelScan(); // stop, then join, before anything the scan reports to goes away
}

void NewReviewDialog::browse()
{
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Choose the Folder of Clips"), folder());
    if (!chosen.isEmpty()) setFolder(chosen);
}

void NewReviewDialog::setFolder(const QString& path)
{
    m_folder->setText(QDir::toNativeSeparators(path));
    startScan();
}

QString NewReviewDialog::folder() const
{
    const QString text = m_folder->text().trimmed();
    return text.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(text));
}

bool NewReviewDialog::includeSubfolders() const
{
    return m_subfolders->isChecked();
}

QString NewReviewDialog::reviewName() const
{
    return m_name->text().trimmed();
}

QString NewReviewDialog::projectPath() const
{
    if (folder().isEmpty() || reviewName().isEmpty()) return {};
    return QDir(folder()).filePath(reviewName() + QStringLiteral(".atkproj"));
}

QVector<review::FoundClip> NewReviewDialog::chosenClips() const
{
    QVector<review::FoundClip> chosen;
    for (int i = 0; i < m_clips->count(); ++i) {
        const QListWidgetItem* item = m_clips->item(i);
        const int index = item->data(kClipIndexRole).toInt();
        if (item->checkState() == Qt::Checked && index >= 0 && index < m_found.size()) chosen.append(m_found[index]);
    }
    return chosen;
}

void NewReviewDialog::cancelScan()
{
    if (!m_scanThread) return;
    m_scanCancel->store(true);
    m_scanThread->wait();
    delete m_scanThread; // its queued finished handler holds only a QPointer
    m_scanThread = nullptr;
}

void NewReviewDialog::startScan()
{
    if (folder() == m_scannedFolder && includeSubfolders() == m_scannedSubfolders && (m_scanThread || !m_found.isEmpty()))
        return; // nothing changed: keep the list and the user's ticks and order
    m_scannedFolder = folder();
    m_scannedSubfolders = includeSubfolders();
    cancelScan();
    m_found.clear();
    m_clips->clear();
    const QString path = folder();
    if (!m_nameEdited) m_name->setText(path.isEmpty() ? QString() : QFileInfo(path).fileName());
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        refreshState();
        return;
    }
    const quint64 token = ++m_scanToken;
    const bool subfolders = includeSubfolders();
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_scanCancel = cancel;
    // The worker posts its result to this dialog; the dialog's destructor
    // cancels and joins it first, so the dialog outlives the call.
    QThread* thread = QThread::create([this, path, subfolders, token, cancel] {
        QVector<review::FoundClip> clips = review::scanFolder(path, subfolders, [cancel] { return cancel->load(); });
        if (cancel->load()) return;
        QMetaObject::invokeMethod(this, [this, token, clips = std::move(clips)] { scanFinished(token, clips); },
                                  Qt::QueuedConnection);
    });
    thread->setObjectName(QStringLiteral("ReviewFolderScan"));
    m_scanThread = thread;
    QPointer<QThread> guard(thread);
    connect(thread, &QThread::finished, this, [this, guard] {
        if (!guard) return;
        if (m_scanThread == guard) m_scanThread = nullptr;
        guard->deleteLater();
        refreshState();
    });
    thread->start();
    refreshState();
}

void NewReviewDialog::scanFinished(quint64 token, const QVector<review::FoundClip>& clips)
{
    if (token != m_scanToken) return; // superseded by a newer scan
    m_found = clips;
    const QSignalBlocker blocker(m_clips);
    m_clips->clear();
    for (int i = 0; i < m_found.size(); ++i) {
        // A sequence shows as "shot.[0001-0096].png" in its folder.
        const QString directory = QFileInfo(m_found[i].relativePath).path();
        const QString shown = !m_found[i].sequence ? m_found[i].relativePath
            : directory == QStringLiteral(".")     ? m_found[i].displayName
                                                   : directory + QLatin1Char('/') + m_found[i].displayName;
        auto* item = new QListWidgetItem(QDir::toNativeSeparators(shown), m_clips);
        item->setData(kClipIndexRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        item->setCheckState(Qt::Checked);
        item->setToolTip(QDir::toNativeSeparators(m_found[i].path));
    }
    applyOrder();
}

void NewReviewDialog::applyOrder()
{
    const Order order = Order(m_order->currentData().toInt());
    QVector<QListWidgetItem*> items;
    while (m_clips->count() > 0) items.append(m_clips->takeItem(0));
    std::stable_sort(items.begin(), items.end(), [&](QListWidgetItem* a, QListWidgetItem* b) {
        const review::FoundClip& ca = m_found[a->data(kClipIndexRole).toInt()];
        const review::FoundClip& cb = m_found[b->data(kClipIndexRole).toInt()];
        if (order == Order::Modified && ca.modifiedMsUtc != cb.modifiedMsUtc) return ca.modifiedMsUtc < cb.modifiedMsUtc;
        return review::naturalLess(ca.relativePath, cb.relativePath);
    });
    for (QListWidgetItem* item : items) m_clips->addItem(item);
    refreshState();
}

void NewReviewDialog::refreshState()
{
    const int chosen = int(chosenClips().size());
    QString problem;
    if (folder().isEmpty()) problem = tr("Choose the folder that holds the clips.");
    else if (!QFileInfo(folder()).isDir()) problem = tr("That folder does not exist.");
    else if (m_scanThread) problem = tr("Looking for clips…");
    else if (m_found.isEmpty()) problem = tr("No video or image files were found in this folder.");
    else if (chosen == 0) problem = tr("Tick at least one clip.");
    else if (!isUsableFileName(reviewName())) problem = tr("Give the review a name that can be a file name.");
    else if (QFileInfo::exists(projectPath()))
        problem = tr("This folder already has a review called \"%1\". Choose another name, or open that one with "
                     "Open Project.").arg(reviewName());
    m_summary->setText(problem.isEmpty()
                           ? tr("%1 of %n clip(s) will be in the review, saved as %2.", nullptr, int(m_found.size()))
                                 .arg(chosen)
                                 .arg(QDir::toNativeSeparators(QFileInfo(projectPath()).fileName()))
                           : problem);
    m_create->setEnabled(problem.isEmpty());
}

} // namespace atk::ui
