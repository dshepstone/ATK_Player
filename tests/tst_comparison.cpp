#include "core/commands/CommandDefinitions.h"
#include "media/MediaSource.h"
#include "playback/CompareSession.h"
#include "playback/CompareVideoLane.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/MainWindow.h"
#include "ui/ViewerWidget.h"

#include <QAction>
#include <QComboBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using atk::playback::CompareLayout;
using atk::playback::ComparePane;
using atk::playback::CompareSession;
using atk::playback::PlayerState;

namespace {
QString media(const char* name)
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/") + QString::fromLatin1(name);
}

QString writeProject(QTemporaryDir& directory)
{
    atk::project::Project project;
    project.setName(QStringLiteral("Comparison test"));
    for (const auto& item : {qMakePair(QStringLiteral("A 24"), media("atk_fixture_48f.mkv")),
                             qMakePair(QStringLiteral("B 59.94"), media("atk_compare_5994fps.mkv")),
                             qMakePair(QStringLiteral("C 30"), media("atk_compare_30fps.mkv"))}) {
        project.addSource(std::make_shared<atk::media::MediaSource>(item.second));
        project.mutableEntries().last().displayName = item.first;
    }
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("comparison.atkproj"));
    return atk::project::ProjectSerializer::save(project, path).ok ? path : QString();
}

QAction* command(atk::ui::MainWindow& window, const char* key)
{
    return window.findChild<QAction*>(QString::fromLatin1(key));
}
}

class TestComparison : public QObject {
    Q_OBJECT
private slots:
    void timestampMappingHasNoCumulativeDrift();
    void vfrPresentationOrderAndClamping();
    void commandDefaultsAreShortcutSafe();
    void enableLayoutActiveViewerAndExitAreNonDestructive();
    void unequalRateLaneTracksAuthoritativeSeek();
    void comparisonEndDoesNotAdvancePlaylist();
};

void TestComparison::timestampMappingHasNoCumulativeDrift()
{
    const atk::media::FrameRate a{24000, 1001};
    const atk::media::FrameRate b{60000, 1001};
    for (qint64 aFrame = 0; aFrame < 24 * 60 * 10; aFrame += 37) {
        const qint64 target = CompareSession::frameTimeUs(aFrame, a);
        const qint64 bFrame = CompareSession::constantRateFrameForTime(target, b, 0, 100000);
        const qint64 actual = CompareSession::frameTimeUs(bFrame, b);
        QVERIFY2(actual <= target, "comparison frame must not lead the authoritative timestamp");
        QVERIFY2(target - actual <= CompareSession::frameTimeUs(1, b),
                 "unequal-rate error must remain bounded by one B frame");
    }
}

void TestComparison::vfrPresentationOrderAndClamping()
{
    const QVector<qint64> pts{0, 40'000, 85'000, 119'000, 200'000};
    QCOMPARE(CompareSession::frameForPts(-1, pts), 0);
    QCOMPARE(CompareSession::frameForPts(84'999, pts), 1);
    QCOMPARE(CompareSession::frameForPts(119'000, pts), 3);
    QCOMPARE(CompareSession::mappedTargetUs(5'000'000, 1'000'000, 2'000'000, 4'000'000), 4'000'000);
    QCOMPARE(CompareSession::mappedTargetUs(500'000, 1'000'000, 2'000'000, 4'000'000), 2'000'000);
    QCOMPARE(CompareSession::mappedTargetUs(2'000'000, 1'000'000, 2'000'000, 4'000'000, -250'000), 2'750'000);
}

void TestComparison::commandDefaultsAreShortcutSafe()
{
    const auto* toggle = atk::commands::find(atk::commands::CommandId::ToggleComparison);
    const auto* side = atk::commands::find(atk::commands::CommandId::CompareSideBySide);
    const auto* stacked = atk::commands::find(atk::commands::CommandId::CompareStacked);
    QVERIFY(toggle && side && stacked);
    QVERIFY(!toggle->defaultShortcut && !side->defaultShortcut && !stacked->defaultShortcut);
}

void TestComparison::enableLayoutActiveViewerAndExitAreNonDestructive()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    const bool modified = window.project()->isModified();
    const int activeIndex = window.project()->activeIndex();
    QAction* toggle = command(window, "view.toggleComparison");
    QVERIFY(toggle && toggle->isEnabled());
    toggle->trigger();
    QTRY_VERIFY(window.isComparisonActive());
    QVERIFY(window.viewerA()->isVisible());
    QVERIFY(window.viewerB()->isVisible());
    QVERIFY(window.compareSession()->sourceAId() != window.compareSession()->sourceBId());
    QCOMPARE(window.project()->isModified(), modified);

    command(window, "view.compareStacked")->trigger();
    QCOMPARE(window.compareSession()->layout(), CompareLayout::Stacked);
    QCOMPARE(window.project()->activeIndex(), activeIndex);
    QTest::mouseClick(window.viewerB(), Qt::LeftButton);
    QCOMPARE(window.compareSession()->activePane(), ComparePane::B);

    QAction* videoFullscreen = command(window, "view.toggleVideoFullScreen");
    QVERIFY(videoFullscreen && !videoFullscreen->isEnabled());
    toggle->trigger();
    QVERIFY(!window.isComparisonActive());
    QVERIFY(videoFullscreen->isEnabled());
    QCOMPARE(window.project()->activeIndex(), activeIndex);
    QCOMPARE(window.project()->isModified(), modified);
}

void TestComparison::unequalRateLaneTracksAuthoritativeSeek()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    QTRY_VERIFY(window.compareVideoLane() && window.compareVideoLane()->isReady());
    QSignalSpy bFrames(window.compareVideoLane(), &atk::playback::CompareVideoLane::frameChanged);
    window.playbackController()->seekFrame(24);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(24), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!bFrames.isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.compareVideoLane()->presentedPtsUs() >= 950'000, 5000);
    QVERIFY(window.compareVideoLane()->presentedPtsUs() <= 1'050'000);
    QVERIFY(window.compareVideoLane()->cacheBytes() <= window.compareVideoLane()->cacheBudgetBytes());
}

void TestComparison::comparisonEndDoesNotAdvancePlaylist()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    window.playbackController()->play();
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ended, 10000);
    QCOMPARE(window.project()->activeIndex(), 0);
    QVERIFY(window.isComparisonActive());
}

QTEST_MAIN(TestComparison)
#include "tst_comparison.moc"
