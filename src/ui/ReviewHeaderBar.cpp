#include "ui/ReviewHeaderBar.h"

#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>

namespace atk::ui {
namespace {

using project::ClipStatus;

constexpr ClipStatus kStatuses[] = {ClipStatus::NotReviewed, ClipStatus::InReview, ClipStatus::NeedsChanges,
                                    ClipStatus::Approved};

QIcon statusDot(const QColor& color)
{
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pixmap(12 * scale, 12 * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(2, 2, 8, 8));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QToolButton* iconButton(QWidget* parent, const QString& icon, const QString& tip, const QString& objectName)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setIcon(icons::icon(icon));
    button->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setProperty("atkRole", "iconButton");
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

QString clipStatusLabel(ClipStatus status)
{
    switch (status) {
    case ClipStatus::NotReviewed: return ReviewHeaderBar::tr("Not reviewed");
    case ClipStatus::InReview: return ReviewHeaderBar::tr("In review");
    case ClipStatus::NeedsChanges: return ReviewHeaderBar::tr("Needs changes");
    case ClipStatus::Approved: return ReviewHeaderBar::tr("Approved");
    }
    return {};
}

QColor clipStatusColor(ClipStatus status)
{
    switch (status) {
    case ClipStatus::NotReviewed: return theme::statusNotReviewed();
    case ClipStatus::InReview: return theme::statusInReview();
    case ClipStatus::NeedsChanges: return theme::statusNeedsChanges();
    case ClipStatus::Approved: return theme::statusApproved();
    }
    return theme::statusNotReviewed();
}

ReviewHeaderBar::ReviewHeaderBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ReviewHeaderBar"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(theme::metrics::spacingMedium, 2, theme::metrics::spacingMedium, 2);
    layout->setSpacing(theme::metrics::spacingMedium);

    m_home = iconButton(this, QStringLiteral("house"), tr("Review Library (Ctrl+Shift+H)"),
                        QStringLiteral("ReviewHeaderHome"));
    connect(m_home, &QToolButton::clicked, this, &ReviewHeaderBar::libraryRequested);
    m_project = new QLabel(this);
    m_project->setObjectName(QStringLiteral("ReviewHeaderProject"));
    m_project->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    m_separator = new QLabel(QStringLiteral("›"), this);
    m_separator->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textDisabled().name()));
    m_clip = new QLabel(this);
    m_clip->setObjectName(QStringLiteral("ReviewHeaderClip"));
    m_clip->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13px;"));
    m_clip->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_status = new QComboBox(this);
    m_status->setObjectName(QStringLiteral("ClipStatusCombo"));
    m_status->setToolTip(tr("Review status of this clip"));
    m_status->setAccessibleName(tr("Review status"));
    m_status->setFocusPolicy(Qt::NoFocus);
    for (ClipStatus status : kStatuses)
        m_status->addItem(statusDot(clipStatusColor(status)), clipStatusLabel(status), int(status));
    connect(m_status, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0) return;
        emit statusChosen(ClipStatus(m_status->itemData(index).toInt()));
    });

    m_export = iconButton(this, QStringLiteral("download"), tr("Export"), QStringLiteral("ReviewHeaderExport"));
    m_export->setPopupMode(QToolButton::InstantPopup);
    m_export->setMenu(new QMenu(m_export));
    m_rename = iconButton(this, QStringLiteral("text-cursor-input"), tr("Rename clips from a class list"),
                          QStringLiteral("ReviewHeaderRename"));
    m_rename->hide(); // shown once the owner hands over its command
    m_folder = iconButton(this, QStringLiteral("folder-open"), tr("Show the clip's folder"),
                          QStringLiteral("ReviewHeaderFolder"));
    connect(m_folder, &QToolButton::clicked, this, &ReviewHeaderBar::showInFolderRequested);

    layout->addWidget(m_home);
    layout->addWidget(m_project);
    layout->addWidget(m_separator);
    layout->addWidget(m_clip, 1);
    layout->addWidget(m_status);
    layout->addSpacing(theme::metrics::spacingMedium);
    layout->addWidget(m_rename);
    layout->addWidget(m_export);
    layout->addWidget(m_folder);

    setLocation(QString(), QString(), false);
}

void ReviewHeaderBar::setLocation(const QString& projectName, const QString& clipName, bool hasClip)
{
    m_project->setText(projectName);
    m_project->setVisible(!projectName.isEmpty());
    m_separator->setVisible(!projectName.isEmpty() && hasClip);
    m_clip->setText(hasClip ? clipName : tr("No clip loaded"));
    m_clip->setEnabled(hasClip);
    m_status->setEnabled(hasClip);
    m_export->setEnabled(hasClip);
    m_folder->setEnabled(hasClip);
}

void ReviewHeaderBar::setStatus(ClipStatus status)
{
    m_updating = true;
    m_status->setCurrentIndex(m_status->findData(int(status)));
    m_updating = false;
}

ClipStatus ReviewHeaderBar::status() const
{
    return ClipStatus(m_status->currentData().toInt());
}

void ReviewHeaderBar::setRenameAction(QAction* action)
{
    m_rename->setDefaultAction(action);
    m_rename->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_rename->setToolTip(tr("Rename clips from a class list"));
    m_rename->show();
}

void ReviewHeaderBar::setExportActions(const QList<QAction*>& actions)
{
    QMenu* menu = m_export->menu();
    menu->clear();
    menu->addActions(actions);
}

} // namespace atk::ui
