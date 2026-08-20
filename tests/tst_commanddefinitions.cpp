#include "core/commands/CommandDefinitions.h"

#include <QKeySequence>
#include <QSet>
#include <QTest>

using namespace atk::commands;

class TestCommandDefinitions : public QObject {
    Q_OBJECT

private slots:
    void tableIsNotEmpty();
    void keysAreUnique();
    void shortcutsAreUnique();
    void lookupByIdWorks();
    void lookupByKeyWorks();
    void unknownKeyReturnsNull();
    void everyCategoryHasATitle();
    void requiredDefaultShortcuts_data();
    void requiredDefaultShortcuts();
    void loopIsCheckable();
    void m2CommandsHaveExpectedDefaults();
};

void TestCommandDefinitions::tableIsNotEmpty()
{
    QVERIFY(!allCommands().empty());
}

void TestCommandDefinitions::keysAreUnique()
{
    // The key is the identifier persisted shortcuts and the external API use;
    // a duplicate would silently bind two commands to one name.
    QSet<QString> seen;
    for (const CommandDefinition& definition : allCommands()) {
        const QString key = QString::fromLatin1(definition.key);
        QVERIFY2(!seen.contains(key), qPrintable(QStringLiteral("duplicate key: %1").arg(key)));
        seen.insert(key);
    }
}

void TestCommandDefinitions::shortcutsAreUnique()
{
    QSet<QString> seen;
    for (const CommandDefinition& definition : allCommands()) {
        if (definition.defaultShortcut == nullptr) {
            continue;
        }
        const QString shortcut =
            QKeySequence(QString::fromLatin1(definition.defaultShortcut)).toString();
        QVERIFY2(!seen.contains(shortcut),
                 qPrintable(QStringLiteral("duplicate default shortcut: %1").arg(shortcut)));
        seen.insert(shortcut);
    }
}

void TestCommandDefinitions::lookupByIdWorks()
{
    const CommandDefinition* definition = find(CommandId::PlayPause);
    QVERIFY(definition != nullptr);
    QCOMPARE(definition->id, CommandId::PlayPause);
}

void TestCommandDefinitions::lookupByKeyWorks()
{
    const CommandDefinition* definition = find(QStringLiteral("playback.playPause"));
    QVERIFY(definition != nullptr);
    QCOMPARE(definition->id, CommandId::PlayPause);
}

void TestCommandDefinitions::unknownKeyReturnsNull()
{
    QVERIFY(find(QStringLiteral("nope.notACommand")) == nullptr);
}

void TestCommandDefinitions::everyCategoryHasATitle()
{
    for (const CommandDefinition& definition : allCommands()) {
        QVERIFY(!categoryTitle(definition.category).isEmpty());
    }
}

void TestCommandDefinitions::requiredDefaultShortcuts_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QString>("shortcut");

    // The bindings the Phase 0 specification requires.
    QTest::newRow("play/pause")     << "playback.playPause"     << "Space";
    QTest::newRow("previous frame") << "playback.previousFrame" << "Left";
    QTest::newRow("next frame")     << "playback.nextFrame"     << "Right";
    QTest::newRow("first frame")    << "playback.firstFrame"    << "Home";
    QTest::newRow("last frame")     << "playback.lastFrame"     << "End";
    QTest::newRow("loop")           << "playback.toggleLoop"    << "L";
    QTest::newRow("range in")       << "playback.setRangeIn"    << "I";
    QTest::newRow("range out")      << "playback.setRangeOut"   << "O";
    QTest::newRow("bookmark")       << "playback.addBookmark"   << "B";
}

void TestCommandDefinitions::requiredDefaultShortcuts()
{
    QFETCH(QString, key);
    QFETCH(QString, shortcut);

    const CommandDefinition* definition = find(key);
    QVERIFY2(definition != nullptr, qPrintable(key));
    QVERIFY2(definition->defaultShortcut != nullptr, qPrintable(key));

    QCOMPARE(QKeySequence(QString::fromLatin1(definition->defaultShortcut)),
             QKeySequence(shortcut));
}

void TestCommandDefinitions::loopIsCheckable()
{
    const CommandDefinition* definition = find(CommandId::ToggleLoop);
    QVERIFY(definition != nullptr);
    QVERIFY(definition->checkable);
}

void TestCommandDefinitions::m2CommandsHaveExpectedDefaults()
{
    const auto* frameStep = find(CommandId::ToggleFrameStepAudio);
    QVERIFY(frameStep && frameStep->checkable);
    QCOMPARE(QKeySequence(QString::fromLatin1(find(CommandId::TimelineZoomIn)->defaultShortcut)), QKeySequence("="));
    QCOMPARE(QKeySequence(QString::fromLatin1(find(CommandId::TimelineZoomOut)->defaultShortcut)), QKeySequence("-"));
    QCOMPARE(QKeySequence(QString::fromLatin1(find(CommandId::TimelineZoomFit)->defaultShortcut)), QKeySequence("F"));
}

QTEST_GUILESS_MAIN(TestCommandDefinitions)
#include "tst_commanddefinitions.moc"
