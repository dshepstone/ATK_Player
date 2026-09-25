#include "ui/WelcomeDialog.h"

#include "core/Version.h"
#include "ui/Theme.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace atk::ui {

WelcomeDialog::WelcomeDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("WelcomeDialog"));
    setWindowTitle(tr("Welcome to ATK Player"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(16);

    // --- Header: logo beside the product name ------------------------------
    auto* header = new QHBoxLayout();
    header->setSpacing(18);
    auto* logo = new QLabel(this);
    logo->setObjectName(QStringLiteral("WelcomeLogo"));
    const QPixmap icon(QStringLiteral(":/icons/ATK_Player_Icon.png"));
    if (!icon.isNull()) {
        const qreal ratio = devicePixelRatioF();
        QPixmap scaled = icon.scaled(QSize(96, 96) * ratio, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(ratio);
        logo->setPixmap(scaled);
    }
    logo->setFixedSize(96, 96);
    header->addWidget(logo, 0, Qt::AlignTop);

    auto* titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(4);
    auto* title = new QLabel(tr("<span style='font-size:20pt; font-weight:600;'>ATK Player</span>"), this);
    auto* subtitle = new QLabel(tr("Animation Tool Kit — Media Player"), this);
    subtitle->setStyleSheet(QStringLiteral("color: %1; font-size: 11pt;")
                                .arg(theme::accent().name()));
    auto* installed = new QLabel(tr("Version %1 is installed and ready to go.")
                                     .arg(QString::fromLatin1(version::kString)), this);
    installed->setStyleSheet(QStringLiteral("color: %1;").arg(theme::textSecondary().name()));
    titleBlock->addStretch();
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    titleBlock->addWidget(installed);
    titleBlock->addStretch();
    header->addLayout(titleBlock, 1);
    layout->addLayout(header);

    // --- Message -------------------------------------------------------------
    const QString linkStyle = QStringLiteral("color: %1; text-decoration: none;")
                                  .arg(theme::accent().name());
    auto* message = new QLabel(this);
    message->setObjectName(QStringLiteral("WelcomeMessage"));
    message->setWordWrap(true);
    message->setTextFormat(Qt::RichText);
    message->setOpenExternalLinks(true);
    message->setTextInteractionFlags(Qt::TextBrowserInteraction);
    message->setText(tr(
        "<p><b>Thank you for installing ATK Player!</b></p>"
        "<p>ATK Player is a frame-accurate review player built for animation teams, "
        "students, educators and production pipelines. Scrub shots back and forth, "
        "step through them frame by frame, bookmark the frames that need attention, "
        "compare takes side by side, and export review material straight back into "
        "your workflow.</p>"
        "<p>ATK Player is a free, <b>open-source</b> project. Your feedback shapes where "
        "it goes next — if something could work better for your team, or you find a "
        "bug, please let us know on "
        "<a style='%1' href='%2'>GitHub</a>.</p>"
        "<p>Created by <b>David Shepstone</b> &nbsp;·&nbsp; "
        "<a style='%1' href='%3'>shepstone.ca</a></p>")
        .arg(linkStyle, feedbackUrl(), websiteUrl()));
    message->setMinimumWidth(460);
    layout->addWidget(message);

    // --- Buttons -------------------------------------------------------------
    auto* buttons = new QDialogButtonBox(this);
    auto* start = buttons->addButton(tr("Get Started"), QDialogButtonBox::AcceptRole);
    start->setObjectName(QStringLiteral("WelcomeGetStarted"));
    start->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    setFixedSize(sizeHint());
}

QString WelcomeDialog::websiteUrl()
{
    return QStringLiteral("https://shepstone.ca");
}

QString WelcomeDialog::feedbackUrl()
{
    return QStringLiteral("https://github.com/dshepstone/ATK_Player/issues");
}

} // namespace atk::ui
