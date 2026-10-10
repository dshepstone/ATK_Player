#include "ui/ExportUi.h"

#include "ui/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace atk::ui {
namespace exportui {

QString styleSheet()
{
    const auto c = [](const QColor& color) { return color.name(QColor::HexRgb); };
    return QStringLiteral(R"CSS(
QDialog { background-color: %1; }
QFrame#ExportHeader { background-color: %2; border-bottom: 1px solid %3; }
QFrame#ExportHeader QLabel { background: transparent; }
QFrame#ExportHeader QLabel#ExportBadge {
    background-color: %4; color: #ffffff; border-radius: 8px;
    font-size: 11px; font-weight: 700; min-width: 44px; max-width: 44px; min-height: 44px; max-height: 44px;
}
QLabel#ExportTitle { font-size: 17px; font-weight: 600; }
QLabel#ExportSummary { color: %5; }
QFrame#ExportCard { background-color: %2; border: 1px solid %3; border-radius: 8px; }
QFrame#ExportCard QLabel, QFrame#ExportCard QCheckBox, QFrame#ExportCard QWidget#ExportRow { background: transparent; }
QLabel#ExportCardTitle { color: %5; font-size: 10px; font-weight: 700; letter-spacing: 1px; }
QLabel#ExportFieldLabel { color: %5; }
QLabel#ExportNote { color: %4; }
QLabel#ExportWarning { color: #e8a33c; }
QFrame#ExportFooter { background-color: %2; border-top: 1px solid %3; }
QFrame#ExportFooter QCheckBox, QFrame#ExportFooter QLabel { background: transparent; }
QPushButton[atkExport="primary"] {
    background-color: %4; color: #ffffff; border: none; border-radius: 6px;
    padding: 7px 22px; font-weight: 600;
}
QPushButton[atkExport="primary"]:hover { background-color: %6; }
QPushButton[atkExport="primary"]:disabled { background-color: %7; color: %8; }
QPushButton[atkExport="secondary"] {
    background: transparent; border: 1px solid %9; border-radius: 6px; padding: 7px 18px;
}
QPushButton[atkExport="secondary"]:hover { background-color: %7; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 14px; height: 14px; border-radius: 4px;
    border: 1px solid %9; background-color: %7;
}
QCheckBox::indicator:hover { border-color: %4; }
QCheckBox::indicator:checked { background-color: %4; border-color: %4; image: none; }
QCheckBox::indicator:disabled { background-color: %1; border-color: %3; }
QProgressBar#ExportProgressBar {
    background-color: %7; border: none; border-radius: 5px; min-height: 10px; max-height: 10px;
}
QProgressBar#ExportProgressBar::chunk { background-color: %4; border-radius: 5px; }
QLabel#ExportPercent { font-size: 22px; font-weight: 600; }
)CSS")
        .arg(c(theme::windowBackground()), c(theme::panelBackground()), c(theme::panelBorder()),
             c(theme::accent()), c(theme::textSecondary()), c(theme::accent().lighter(115)),
             c(theme::controlBackground()), c(theme::textDisabled()), c(theme::controlHover()));
}

QLabel* addHeader(QVBoxLayout* root, QWidget* parent, const QString& badge, const QString& title,
                  const QString& summary)
{
    auto* header = new QFrame(parent);
    header->setObjectName(QStringLiteral("ExportHeader"));
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(20, 16, 20, 16);
    row->setSpacing(14);
    auto* badgeLabel = new QLabel(badge, header);
    badgeLabel->setObjectName(QStringLiteral("ExportBadge"));
    badgeLabel->setAlignment(Qt::AlignCenter);
    row->addWidget(badgeLabel, 0, Qt::AlignTop);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* titleLabel = new QLabel(title, header);
    titleLabel->setObjectName(QStringLiteral("ExportTitle"));
    auto* summaryLabel = new QLabel(summary, header);
    summaryLabel->setObjectName(QStringLiteral("ExportSummary"));
    summaryLabel->setWordWrap(true);
    text->addWidget(titleLabel);
    text->addWidget(summaryLabel);
    row->addLayout(text, 1);
    root->addWidget(header);
    return summaryLabel;
}

QVBoxLayout* addBody(QVBoxLayout* root)
{
    auto* body = new QVBoxLayout;
    body->setContentsMargins(20, 16, 20, 16);
    body->setSpacing(12);
    root->addLayout(body, 1);
    return body;
}

QVBoxLayout* addCard(QVBoxLayout* body, QWidget* parent, const QString& title)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("ExportCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(9);
    auto* titleLabel = new QLabel(title.toUpper(), card);
    titleLabel->setObjectName(QStringLiteral("ExportCardTitle"));
    layout->addWidget(titleLabel);
    body->addWidget(card);
    return layout;
}

QHBoxLayout* addRow(QVBoxLayout* card, QWidget* parent, const QString& label, QWidget* field, int stretch)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    auto* labelWidget = new QLabel(label, parent);
    labelWidget->setObjectName(QStringLiteral("ExportFieldLabel"));
    labelWidget->setMinimumWidth(96);
    row->addWidget(labelWidget);
    if (field) row->addWidget(field, stretch);
    if (stretch == 0) row->addStretch(1);
    card->addLayout(row);
    return row;
}

QPushButton* addFooter(QVBoxLayout* root, QDialog* dialog, const QString& primaryText, QWidget* left)
{
    auto* footer = new QFrame(dialog);
    footer->setObjectName(QStringLiteral("ExportFooter"));
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(20, 12, 20, 12);
    row->setSpacing(10);
    if (left) {
        left->setParent(footer);
        row->addWidget(left);
    }
    row->addStretch(1);
    auto* cancel = new QPushButton(QObject::tr("Cancel"), footer);
    cancel->setObjectName(QStringLiteral("ExportCancel"));
    cancel->setProperty("atkExport", QStringLiteral("secondary"));
    auto* primary = new QPushButton(primaryText, footer);
    primary->setObjectName(QStringLiteral("ExportPrimary"));
    primary->setProperty("atkExport", QStringLiteral("primary"));
    primary->setDefault(true);
    row->addWidget(cancel);
    row->addWidget(primary);
    root->addWidget(footer);
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(primary, &QPushButton::clicked, dialog, &QDialog::accept);
    return primary;
}

} // namespace exportui

ExportProgressDialog::ExportProgressDialog(const QString& badge, const QString& title, const QString& fileName,
                                           QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ExportProgressDialog"));
    setWindowTitle(title);
    setStyleSheet(exportui::styleSheet());
    setMinimumWidth(460);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    exportui::addHeader(root, this, badge, title, fileName);
    auto* body = exportui::addBody(root);

    auto* top = new QHBoxLayout;
    m_status = new QLabel(tr("Preparing…"), this);
    m_status->setObjectName(QStringLiteral("ExportProgressStatus"));
    m_percent = new QLabel(QStringLiteral("0%"), this);
    m_percent->setObjectName(QStringLiteral("ExportPercent"));
    top->addWidget(m_status, 1, Qt::AlignBottom);
    top->addWidget(m_percent, 0, Qt::AlignBottom);
    body->addLayout(top);
    m_bar = new QProgressBar(this);
    m_bar->setObjectName(QStringLiteral("ExportProgressBar"));
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    body->addWidget(m_bar);
    m_remaining = new QLabel(QString(), this);
    m_remaining->setObjectName(QStringLiteral("ExportSummary"));
    body->addWidget(m_remaining);

    auto* footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("ExportFooter"));
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(20, 12, 20, 12);
    row->addStretch(1);
    m_cancel = new QPushButton(tr("Cancel"), footer);
    m_cancel->setObjectName(QStringLiteral("ExportProgressCancel"));
    m_cancel->setProperty("atkExport", QStringLiteral("secondary"));
    row->addWidget(m_cancel);
    root->addWidget(footer);
    connect(m_cancel, &QPushButton::clicked, this, &ExportProgressDialog::reject);
    m_clock.start();
}

int ExportProgressDialog::value() const { return m_bar->value(); }

void ExportProgressDialog::setValue(int percent)
{
    percent = std::clamp(percent, 0, 100);
    m_bar->setValue(percent);
    m_percent->setText(QStringLiteral("%1%").arg(percent));
    // Time left from the pace so far, once there is enough of it to trust.
    const qint64 elapsed = m_clock.elapsed();
    if (percent >= 100) {
        m_remaining->setText(tr("Finishing…"));
    } else if (percent >= 3 && elapsed > 1500) {
        const qint64 left = qint64(std::llround(elapsed * (100.0 - percent) / percent / 1000.0));
        m_remaining->setText(left < 60 ? tr("About %n second(s) left", nullptr, int(std::max<qint64>(1, left)))
                                       : tr("About %n minute(s) left", nullptr, int((left + 30) / 60)));
    }
}

void ExportProgressDialog::setLabelText(const QString& text) { m_status->setText(text); }

void ExportProgressDialog::finish()
{
    m_finishing = true;
    QDialog::reject(); // hides the window
}

void ExportProgressDialog::reject()
{
    // Cancel (or the window's close button) asks the export to stop; the owner
    // calls finish() once it has.
    if (m_finishing) {
        QDialog::reject();
        return;
    }
    if (m_cancelled) return;
    m_cancelled = true;
    m_cancel->setEnabled(false);
    m_status->setText(tr("Cancelling…"));
    emit canceled();
}

} // namespace atk::ui
