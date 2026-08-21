#include "ui/PreferencesDialog.h"

#include "core/commands/CommandDefinitions.h"
#include "ui/ApplicationSettings.h"
#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace atk::ui {

PreferencesDialog::PreferencesDialog(const ApplicationSettings& settings,
                                     const CommandRegistry& registry,
                                     QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("PreferencesDialog"));
    setWindowTitle(tr("Preferences"));
    setModal(true);
    resize(620, 500);

    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    auto* general = new QWidget(tabs);
    auto* generalLayout = new QVBoxLayout(general);
    m_restoreLayout = new QCheckBox(tr("Restore window size, position and dock layout on startup"), general);
    m_restoreLayout->setObjectName(QStringLiteral("RestoreWindowLayout"));
    m_restoreLayout->setChecked(settings.restoreWindowLayout());
    generalLayout->addWidget(m_restoreLayout);
    m_reopenLast = new QCheckBox(tr("Reopen last project on startup"), general);
    m_reopenLast->setObjectName(QStringLiteral("ReopenLastProject"));
    m_reopenLast->setChecked(settings.reopenLastProject());
    generalLayout->addWidget(m_reopenLast);
    auto* resetPreferences = new QPushButton(tr("Reset Preferences to Defaults"), general);
    resetPreferences->setObjectName(QStringLiteral("ResetPreferences"));
    generalLayout->addWidget(resetPreferences, 0, Qt::AlignLeft);
    generalLayout->addStretch();
    tabs->addTab(general, tr("General"));

    auto* review = new QWidget(tabs);
    auto* reviewLayout = new QVBoxLayout(review);
    m_audioScrub = new QCheckBox(tr("Audio Scrub"), review);
    m_audioScrub->setObjectName(QStringLiteral("PreferenceAudioScrub"));
    m_audioScrub->setChecked(settings.audioScrubEnabled());
    m_frameStepAudio = new QCheckBox(tr("Frame-Step Audio"), review);
    m_frameStepAudio->setObjectName(QStringLiteral("PreferenceFrameStepAudio"));
    m_frameStepAudio->setChecked(settings.frameStepAudioEnabled());
    m_bookmarkSnap = new QCheckBox(tr("Snap to Bookmarks"), review);
    m_bookmarkSnap->setObjectName(QStringLiteral("PreferenceBookmarkSnap"));
    m_bookmarkSnap->setChecked(settings.bookmarkSnapEnabled());
    reviewLayout->addWidget(m_audioScrub);
    reviewLayout->addWidget(m_frameStepAudio);
    reviewLayout->addWidget(m_bookmarkSnap);
    reviewLayout->addStretch();
    tabs->addTab(review, tr("Review"));

    for (const commands::CommandDefinition& definition : commands::allCommands()) {
        const QAction* action = registry.action(definition.id);
        m_shortcuts.insert(QString::fromLatin1(definition.key),
                           action ? action->shortcut().toString(QKeySequence::PortableText) : QString());
    }

    auto* shortcuts = new QWidget(tabs);
    auto* shortcutLayout = new QVBoxLayout(shortcuts);
    m_shortcutTable = new QTableWidget(0, 3, shortcuts);
    m_shortcutTable->setObjectName(QStringLiteral("ShortcutTable"));
    m_shortcutTable->setHorizontalHeaderLabels({tr("Command"), tr("Current Shortcut"), tr("Default")});
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_shortcutTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_shortcutTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shortcutLayout->addWidget(m_shortcutTable, 1);
    auto* editorForm = new QFormLayout;
    m_shortcutEdit = new QKeySequenceEdit(shortcuts);
    m_shortcutEdit->setObjectName(QStringLiteral("ShortcutEditor"));
    editorForm->addRow(tr("Selected command"), m_shortcutEdit);
    shortcutLayout->addLayout(editorForm);
    auto* shortcutButtons = new QHBoxLayout;
    m_clearShortcut = new QPushButton(tr("Clear"), shortcuts);
    m_clearShortcut->setObjectName(QStringLiteral("ClearShortcut"));
    m_resetSelected = new QPushButton(tr("Reset Selected"), shortcuts);
    m_resetSelected->setObjectName(QStringLiteral("ResetSelectedShortcut"));
    auto* resetAll = new QPushButton(tr("Reset All Shortcuts"), shortcuts);
    resetAll->setObjectName(QStringLiteral("ResetAllShortcuts"));
    shortcutButtons->addWidget(m_clearShortcut);
    shortcutButtons->addWidget(m_resetSelected);
    shortcutButtons->addWidget(resetAll);
    shortcutButtons->addStretch();
    shortcutLayout->addLayout(shortcutButtons);
    tabs->addTab(shortcuts, tr("Shortcuts"));

    root->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    populateShortcutTable();
    connect(m_shortcutTable, &QTableWidget::currentCellChanged,
            this, [this] { loadSelectedShortcut(); });
    connect(m_shortcutEdit, &QKeySequenceEdit::keySequenceChanged,
            this, &PreferencesDialog::assignSelectedShortcut);
    connect(m_clearShortcut, &QPushButton::clicked, this,
            [this] { assignSelectedShortcut(QKeySequence()); });
    connect(m_resetSelected, &QPushButton::clicked, this, &PreferencesDialog::resetSelectedShortcut);
    connect(resetAll, &QPushButton::clicked, this, &PreferencesDialog::resetAllShortcuts);
    connect(resetPreferences, &QPushButton::clicked, this, &PreferencesDialog::resetPreferencesDraft);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (m_shortcutTable->rowCount() > 0) m_shortcutTable->setCurrentCell(0, 0);
}

bool PreferencesDialog::restoreWindowLayout() const { return m_restoreLayout->isChecked(); }
bool PreferencesDialog::audioScrubEnabled() const { return m_audioScrub->isChecked(); }
bool PreferencesDialog::frameStepAudioEnabled() const { return m_frameStepAudio->isChecked(); }
bool PreferencesDialog::bookmarkSnapEnabled() const { return m_bookmarkSnap->isChecked(); }
bool PreferencesDialog::reopenLastProject() const { return m_reopenLast->isChecked(); }

QString PreferencesDialog::conflictingCommand(const QHash<QString, QString>& shortcuts,
                                              const QString& commandKey,
                                              const QKeySequence& sequence)
{
    if (sequence.isEmpty()) return {};
    const QString portable = sequence.toString(QKeySequence::PortableText);
    for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it)
        if (it.key() != commandKey && it.value() == portable) return it.key();
    return {};
}

void PreferencesDialog::populateShortcutTable()
{
    for (const commands::CommandDefinition& definition : commands::allCommands()) {
        const int row = m_shortcutTable->rowCount();
        m_shortcutTable->insertRow(row);
        auto* name = new QTableWidgetItem(QCoreApplication::translate("Command", definition.displayName));
        name->setData(Qt::UserRole, QString::fromLatin1(definition.key));
        m_shortcutTable->setItem(row, 0, name);
        m_shortcutTable->setItem(row, 1, new QTableWidgetItem);
        m_shortcutTable->setItem(row, 2, new QTableWidgetItem(
            definition.defaultShortcut ? QKeySequence(QString::fromLatin1(definition.defaultShortcut)).toString(QKeySequence::NativeText)
                                       : tr("Unassigned")));
        updateShortcutRow(QString::fromLatin1(definition.key));
    }
}

void PreferencesDialog::updateShortcutRow(const QString& commandKey)
{
    for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
        if (m_shortcutTable->item(row, 0)->data(Qt::UserRole).toString() != commandKey) continue;
        const QKeySequence sequence = QKeySequence::fromString(m_shortcuts.value(commandKey), QKeySequence::PortableText);
        m_shortcutTable->item(row, 1)->setText(sequence.isEmpty() ? tr("Unassigned")
                                                                  : sequence.toString(QKeySequence::NativeText));
        return;
    }
}

void PreferencesDialog::loadSelectedShortcut()
{
    const int row = m_shortcutTable->currentRow();
    const bool enabled = row >= 0;
    m_shortcutEdit->setEnabled(enabled);
    m_clearShortcut->setEnabled(enabled);
    m_resetSelected->setEnabled(enabled);
    if (!enabled) return;
    m_loadingShortcut = true;
    const QString key = m_shortcutTable->item(row, 0)->data(Qt::UserRole).toString();
    m_shortcutEdit->setKeySequence(QKeySequence::fromString(m_shortcuts.value(key), QKeySequence::PortableText));
    m_loadingShortcut = false;
}

void PreferencesDialog::assignSelectedShortcut(const QKeySequence& sequence)
{
    if (m_loadingShortcut || m_shortcutTable->currentRow() < 0) return;
    const QString key = m_shortcutTable->item(m_shortcutTable->currentRow(), 0)->data(Qt::UserRole).toString();
    const QString conflict = conflictingCommand(m_shortcuts, key, sequence);
    if (!conflict.isEmpty()) {
        const commands::CommandDefinition* definition = commands::find(conflict);
        const QString name = definition ? QCoreApplication::translate("Command", definition->displayName) : conflict;
        const auto answer = QMessageBox::question(this, tr("Shortcut Conflict"),
            tr("%1 is already assigned to %2. Replace the existing assignment?")
                .arg(sequence.toString(QKeySequence::NativeText), name),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) { loadSelectedShortcut(); return; }
        m_shortcuts[conflict].clear();
        updateShortcutRow(conflict);
    }
    m_shortcuts[key] = sequence.toString(QKeySequence::PortableText);
    updateShortcutRow(key);
    if (m_shortcutEdit->keySequence() != sequence) {
        m_loadingShortcut = true;
        m_shortcutEdit->setKeySequence(sequence);
        m_loadingShortcut = false;
    }
}

void PreferencesDialog::resetSelectedShortcut()
{
    const int row = m_shortcutTable->currentRow();
    if (row < 0) return;
    const QString key = m_shortcutTable->item(row, 0)->data(Qt::UserRole).toString();
    const commands::CommandDefinition* definition = commands::find(key);
    m_shortcuts[key] = definition && definition->defaultShortcut
        ? QString::fromLatin1(definition->defaultShortcut) : QString();
    updateShortcutRow(key);
    loadSelectedShortcut();
}

void PreferencesDialog::resetAllShortcuts()
{
    for (const commands::CommandDefinition& definition : commands::allCommands()) {
        const QString key = QString::fromLatin1(definition.key);
        m_shortcuts[key] = definition.defaultShortcut ? QString::fromLatin1(definition.defaultShortcut) : QString();
        updateShortcutRow(key);
    }
    loadSelectedShortcut();
}

void PreferencesDialog::resetPreferencesDraft()
{
    if (QMessageBox::question(this, tr("Reset Preferences"),
            tr("Restore all application preferences and shortcuts to their defaults?"),
            QMessageBox::Reset | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Reset) return;
    m_restoreLayout->setChecked(ApplicationSettings::defaultRestoreWindowLayout());
    m_reopenLast->setChecked(ApplicationSettings::defaultReopenLastProject());
    m_audioScrub->setChecked(ApplicationSettings::defaultAudioScrubEnabled());
    m_frameStepAudio->setChecked(ApplicationSettings::defaultFrameStepAudioEnabled());
    m_bookmarkSnap->setChecked(ApplicationSettings::defaultBookmarkSnapEnabled());
    resetAllShortcuts();
    m_resetAllRequested = true;
}

} // namespace atk::ui
