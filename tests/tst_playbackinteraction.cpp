#include "playback/PlaybackController.h"

#include "timeline/TimelineModel.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using atk::playback::PlaybackController;
using atk::playback::PlayerState;
using atk::timeline::TimelineModel;

namespace {

QString syncFixture()
{
    return QDir(QString::fromUtf8(ATK_TEST_MEDIA_DIR))
        .filePath(QStringLiteral("atk_sync_10s.mkv"));
}

struct Fixture {
    TimelineModel timeline;
    PlaybackController playback{ &timeline };

    bool open()
    {
        QSignalSpy opened(&playback, &PlaybackController::mediaOpened);
        playback.openMedia(syncFixture());
        return opened.wait(10000) && playback.state() == PlayerState::Ready;
    }
};

} // namespace

class TestPlaybackInteraction : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void rapidForwardAndBackwardAccumulate();
    void scrubCoalescesToExactReleaseFrame();
    void explicitSeekResetsLogicalTarget();
    void realTimePlaybackCrossesLoopBoundary();
};

void TestPlaybackInteraction::initTestCase()
{
    if (!QFileInfo::exists(syncFixture())) {
        QSKIP("Generated sync fixture not found; build atk_test_media first.");
    }
}

void TestPlaybackInteraction::rapidForwardAndBackwardAccumulate()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    for (int i = 0; i < 10; ++i) {
        fixture.playback.stepForward();
    }
    for (int i = 0; i < 3; ++i) {
        fixture.playback.stepBackward();
    }

    QCOMPARE(fixture.playback.navigationFrame(), qint64(7));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(7), 10000);
    QCOMPARE(fixture.playback.currentFrame(), qint64(7));
}

void TestPlaybackInteraction::scrubCoalescesToExactReleaseFrame()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    fixture.playback.beginScrub();
    for (const int64_t target : { 24, 48, 72, 96, 120, 144 }) {
        fixture.playback.scrubToFrame(target);
    }
    fixture.playback.endScrub(156);

    QCOMPARE(fixture.playback.navigationFrame(), qint64(156));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(156), 10000);
    QCOMPARE(fixture.playback.state(), PlayerState::Ready);
}

void TestPlaybackInteraction::explicitSeekResetsLogicalTarget()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    for (int i = 0; i < 8; ++i) {
        fixture.playback.stepForward();
    }
    QCOMPARE(fixture.playback.navigationFrame(), qint64(8));

    fixture.playback.seekFrame(100);
    QCOMPARE(fixture.playback.navigationFrame(), qint64(100));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(100), 10000);
}

void TestPlaybackInteraction::realTimePlaybackCrossesLoopBoundary()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    fixture.playback.setMuted(true);
    fixture.playback.setLoopEnabled(true);
    QSignalSpy frames(&fixture.playback, &PlaybackController::frameChanged);
    QElapsedTimer elapsed;
    elapsed.start();
    fixture.playback.play();

    // The fixture is ten seconds. At 10.5 seconds playback must have crossed
    // the loop boundary and be advancing from a fresh epoch near the start.
    QTest::qWait(11'500);

    const double seconds = elapsed.elapsed() / 1000.0;
    const double observedFps = frames.count() / seconds;
    qInfo().noquote()
        << QStringLiteral("Release-observable playback %1 fps, drops %2, loop frame %3")
               .arg(observedFps, 0, 'f', 2)
               .arg(fixture.playback.droppedFrameCount())
               .arg(fixture.playback.currentFrame());
    QCOMPARE(fixture.playback.state(), PlayerState::Playing);
    QVERIFY(fixture.playback.currentFrame() >= 2);
    QVERIFY(fixture.playback.currentFrame() < 50);
    QVERIFY(observedFps > 20.0);
    QVERIFY(fixture.playback.droppedFrameCount() < 12);
    fixture.playback.stop();
}

QTEST_GUILESS_MAIN(TestPlaybackInteraction)
#include "tst_playbackinteraction.moc"
