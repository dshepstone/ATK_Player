#include "core/commands/CommandDefinitions.h"
#include "ui/ApplicationSettings.h"
#include "ui/PreferencesDialog.h"
#include "ui/MainWindow.h"
#include "ui/TimelineWidget.h"
#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QKeySequence>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTableWidget>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QWidget>

using atk::commands::CommandId;
using atk::ui::ApplicationSettings;
using atk::ui::CommandRegistry;
using atk::ui::PreferencesDialog;

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void defaultsValidationAndPersistence();
    void shortcutOverridesDistinguishClearAndDefault();
    void registryUpdatesActionsAtRuntime();
    void conflictDetectionIdentifiesStableCommand();
    void preferencesCancelDoesNotWriteSettings();
    void textEditingKeepsTypingShortcuts();
    void mainWindowLoadsReviewAndShortcutPreferences();
    void windowAndDockStateRoundTrip();
    void transportIconsAndTooltipsReuseActions();
    void requiredM2ShortcutDefaultsRemainSafe();
    void shortcutEditorClearAndResetSelected();
};

void TestSettings::defaultsValidationAndPersistence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    ApplicationSettings settings(file);
    QVERIFY(settings.restoreWindowLayout());
    QVERIFY(settings.audioScrubEnabled());
    QVERIFY(!settings.frameStepAudioEnabled());
    QVERIFY(settings.bookmarkSnapEnabled());
    QCOMPARE(settings.volume(), 1.0);
    settings.setAudioScrubEnabled(false);
    settings.setFrameStepAudioEnabled(true);
    settings.setBookmarkSnapEnabled(false);
    settings.setRestoreWindowLayout(false);
    settings.setVolume(0.35);
    settings.setWindowGeometry(QByteArray("geometry"));
    settings.setWindowState(QByteArray("state"));
    settings.sync();

    ApplicationSettings reopened(file);
    QVERIFY(!reopened.restoreWindowLayout());
    QVERIFY(!reopened.audioScrubEnabled());
    QVERIFY(reopened.frameStepAudioEnabled());
    QVERIFY(!reopened.bookmarkSnapEnabled());
    QCOMPARE(reopened.volume(), 0.35);
    QCOMPARE(reopened.windowGeometry(), QByteArray("geometry"));
    QCOMPARE(reopened.windowState(), QByteArray("state"));

    QSettings raw(file, QSettings::IniFormat);
    raw.setValue(QStringLiteral("review/volume"), QStringLiteral("invalid"));
    raw.setValue(QStringLiteral("review/audioScrub"), QStringLiteral("maybe"));
    raw.sync();
    ApplicationSettings invalid(file);
    QCOMPARE(invalid.volume(), ApplicationSettings::defaultVolume());
    QCOMPARE(invalid.audioScrubEnabled(), ApplicationSettings::defaultAudioScrubEnabled());
    invalid.resetAll();
    QVERIFY(invalid.restoreWindowLayout());
    QVERIFY(invalid.audioScrubEnabled());
    QVERIFY(!invalid.frameStepAudioEnabled());
    QVERIFY(invalid.bookmarkSnapEnabled());
}

void TestSettings::shortcutOverridesDistinguishClearAndDefault()
{
    QTemporaryDir directory;
    ApplicationSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(settings.setShortcutOverride(QStringLiteral("view.zoomFit"), QStringLiteral("Ctrl+Alt+F")));
    QVERIFY(settings.setShortcutOverride(QStringLiteral("playback.addRangeBookmark"), QString()));
    QVERIFY(!settings.setShortcutOverride(QStringLiteral("unknown.command"), QStringLiteral("F9")));
    settings.sync();
    const auto overrides = settings.shortcutOverrides();
    QCOMPARE(overrides.value(QStringLiteral("view.zoomFit")), QStringLiteral("Ctrl+Alt+F"));
    QVERIFY(overrides.contains(QStringLiteral("playback.addRangeBookmark")));
    QVERIFY(overrides.value(QStringLiteral("playback.addRangeBookmark")).isEmpty());
    settings.resetShortcutOverride(QStringLiteral("view.zoomFit"));
    QVERIFY(!settings.shortcutOverrides().contains(QStringLiteral("view.zoomFit")));
    settings.resetAllShortcuts();
    QVERIFY(settings.shortcutOverrides().isEmpty());
}

void TestSettings::registryUpdatesActionsAtRuntime()
{
    CommandRegistry registry;
    QAction* fit = registry.action(CommandId::ZoomFit);
    QVERIFY(fit);
    QSignalSpy changed(fit, &QAction::changed);
    registry.setShortcut(CommandId::ZoomFit, QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    QCOMPARE(fit->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    QVERIFY(changed.count() > 0);
    registry.setShortcut(CommandId::ZoomFit, QKeySequence());
    QVERIFY(fit->shortcut().isEmpty());
    registry.resetShortcutsToDefaults();
    QCOMPARE(fit->shortcut(), QKeySequence(QStringLiteral("Ctrl+0")));
}

void TestSettings::conflictDetectionIdentifiesStableCommand()
{
    QHash<QString, QString> shortcuts;
    shortcuts.insert(QStringLiteral("playback.addBookmark"), QStringLiteral("B"));
    shortcuts.insert(QStringLiteral("view.zoomFit"), QStringLiteral("Ctrl+0"));
    QCOMPARE(PreferencesDialog::conflictingCommand(
        shortcuts, QStringLiteral("view.zoomFit"), QKeySequence(QStringLiteral("B"))),
        QStringLiteral("playback.addBookmark"));
    QVERIFY(PreferencesDialog::conflictingCommand(
        shortcuts, QStringLiteral("view.zoomFit"), QKeySequence()).isEmpty());
}

void TestSettings::preferencesCancelDoesNotWriteSettings()
{
    QTemporaryDir directory;
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    ApplicationSettings settings(file);
    CommandRegistry registry;
    PreferencesDialog dialog(settings, registry);
    auto* audio = dialog.findChild<QCheckBox*>(QStringLiteral("PreferenceAudioScrub"));
    QVERIFY(audio);
    audio->setChecked(false);
    dialog.reject();
    ApplicationSettings reopened(file);
    QVERIFY(reopened.audioScrubEnabled());
}

void TestSettings::textEditingKeepsTypingShortcuts()
{
    QWidget window;
    CommandRegistry registry(&window);
    window.addActions(registry.allActions());
    QLineEdit edit(&window);
    window.show();
    edit.setFocus();
    QSignalSpy triggered(&registry, &CommandRegistry::commandTriggered);
    QTest::keyClick(&edit, Qt::Key_B);
    QCOMPARE(edit.text(), QStringLiteral("b"));
    QCOMPARE(triggered.count(), 0);
}

void TestSettings::mainWindowLoadsReviewAndShortcutPreferences()
{
    QTemporaryDir directory;
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    ApplicationSettings settings(file);
    settings.setAudioScrubEnabled(false);
    settings.setFrameStepAudioEnabled(true);
    settings.setBookmarkSnapEnabled(false);
    settings.setRestoreWindowLayout(false);
    QVERIFY(settings.setShortcutOverride(QStringLiteral("view.zoomFit"), QStringLiteral("Ctrl+Alt+F")));
    settings.sync();

    atk::ui::MainWindow window(file);
    QVERIFY(!window.playbackController()->isAudioScrubEnabled());
    QVERIFY(window.playbackController()->isFrameStepAudioEnabled());
    auto* snap = window.findChild<QAction*>(QStringLiteral("playback.snapBookmarks"));
    auto* fit = window.findChild<QAction*>(QStringLiteral("view.zoomFit"));
    auto* timeline = window.findChild<atk::ui::TimelineWidget*>();
    QVERIFY(snap && fit && timeline);
    QVERIFY(!snap->isChecked());
    QVERIFY(!timeline->isBookmarkSnapEnabled());
    QCOMPARE(fit->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    QCOMPARE(window.size(), QSize(1280, 800));
}

void TestSettings::windowAndDockStateRoundTrip()
{
    QTemporaryDir directory;
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    {
        ApplicationSettings settings(file);
        settings.setRestoreWindowLayout(true);
        settings.sync();
        atk::ui::MainWindow window(file);
        window.resize(920, 640);
        auto* bookmarks = window.findChild<QDockWidget*>(QStringLiteral("BookmarksDock"));
        QVERIFY(bookmarks);
        bookmarks->hide();
    }
    {
        atk::ui::MainWindow restored(file);
        auto* bookmarks = restored.findChild<QDockWidget*>(QStringLiteral("BookmarksDock"));
        QVERIFY(bookmarks);
        QCOMPARE(restored.size(), QSize(920, 640));
        QVERIFY(bookmarks->isHidden());
    }
}

void TestSettings::transportIconsAndTooltipsReuseActions()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QAction* play = window.findChild<QAction*>(QStringLiteral("playback.playPause"));
    QAction* bookmark = window.findChild<QAction*>(QStringLiteral("playback.addBookmark"));
    QVERIFY(play && bookmark && !play->icon().isNull() && !bookmark->icon().isNull());
    QToolButton* playButton = nullptr;
    for (QToolButton* button : window.findChildren<QToolButton*>())
        if (button->defaultAction() == play) { playButton = button; break; }
    QVERIFY(playButton);
    QVERIFY(playButton->toolTip().contains(play->shortcut().toString(QKeySequence::NativeText)));
    play->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+P")));
    QVERIFY(playButton->toolTip().contains(QStringLiteral("Ctrl+Alt+P")));
}

void TestSettings::requiredM2ShortcutDefaultsRemainSafe()
{
    const auto shortcut = [](CommandId id) {
        const auto* definition = atk::commands::find(id);
        return definition && definition->defaultShortcut
            ? QKeySequence(QString::fromLatin1(definition->defaultShortcut)) : QKeySequence();
    };
    QCOMPARE(shortcut(CommandId::AddBookmark), QKeySequence(QStringLiteral("B")));
    QCOMPARE(shortcut(CommandId::PreviousBookmark), QKeySequence(QStringLiteral("Alt+Left")));
    QCOMPARE(shortcut(CommandId::NextBookmark), QKeySequence(QStringLiteral("Alt+Right")));
    QCOMPARE(shortcut(CommandId::TimelineZoomFit), QKeySequence(QStringLiteral("F")));
    QCOMPARE(shortcut(CommandId::ZoomFit), QKeySequence(QStringLiteral("Ctrl+0")));
    QCOMPARE(shortcut(CommandId::ZoomActualSize), QKeySequence(QStringLiteral("Ctrl+1")));
    QVERIFY(shortcut(CommandId::AddRangeBookmark).isEmpty());
}

void TestSettings::shortcutEditorClearAndResetSelected()
{
    QTemporaryDir directory;
    ApplicationSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    CommandRegistry registry;
    PreferencesDialog dialog(settings, registry);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("ShortcutTable"));
    auto* editor = dialog.findChild<QKeySequenceEdit*>(QStringLiteral("ShortcutEditor"));
    auto* clear = dialog.findChild<QPushButton*>(QStringLiteral("ClearShortcut"));
    auto* reset = dialog.findChild<QPushButton*>(QStringLiteral("ResetSelectedShortcut"));
    QVERIFY(table && editor && clear && reset);
    int zoomFitRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toString() == QStringLiteral("view.zoomFit"))
            zoomFitRow = row;
    QVERIFY(zoomFitRow >= 0);
    table->setCurrentCell(zoomFitRow, 0);
    editor->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    QCOMPARE(dialog.shortcuts().value(QStringLiteral("view.zoomFit")), QStringLiteral("Ctrl+Alt+F"));
    QTest::mouseClick(clear, Qt::LeftButton);
    QVERIFY(dialog.shortcuts().value(QStringLiteral("view.zoomFit")).isEmpty());
    QVERIFY(editor->keySequence().isEmpty());
    QTest::mouseClick(reset, Qt::LeftButton);
    QCOMPARE(dialog.shortcuts().value(QStringLiteral("view.zoomFit")), QStringLiteral("Ctrl+0"));
}

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
