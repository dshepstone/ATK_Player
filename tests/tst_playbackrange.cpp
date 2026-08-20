#include "timeline/PlaybackRange.h"

#include <QTest>

using atk::timeline::PlaybackRange;

class TestPlaybackRange : public QObject {
    Q_OBJECT

private slots:
    void defaultRangeIsDisabled();
    void frameCountIsInclusive();
    void invalidWhenEndPrecedesStart();
    void containsIsInclusive();
    void clampLeavesFrameAloneWhenDisabled();
    void clampConfinesFrameWhenEnabled();
};

void TestPlaybackRange::defaultRangeIsDisabled()
{
    const PlaybackRange range;
    QVERIFY(!range.enabled);
    QCOMPARE(range.startFrame, qint64(0));
    QCOMPARE(range.endFrame, qint64(0));
}

void TestPlaybackRange::frameCountIsInclusive()
{
    // Frames 10..20 is eleven frames, not ten -- an off-by-one here would drop
    // the last frame of every loop.
    const PlaybackRange range{ 10, 20, true };
    QCOMPARE(range.frameCount(), qint64(11));

    const PlaybackRange single{ 5, 5, true };
    QCOMPARE(single.frameCount(), qint64(1));
}

void TestPlaybackRange::invalidWhenEndPrecedesStart()
{
    const PlaybackRange range{ 20, 10, true };
    QVERIFY(!range.isValid());
    QCOMPARE(range.frameCount(), qint64(0));
}

void TestPlaybackRange::containsIsInclusive()
{
    const PlaybackRange range{ 10, 20, true };
    QVERIFY(range.contains(10));
    QVERIFY(range.contains(15));
    QVERIFY(range.contains(20));
    QVERIFY(!range.contains(9));
    QVERIFY(!range.contains(21));
}

void TestPlaybackRange::clampLeavesFrameAloneWhenDisabled()
{
    const PlaybackRange range{ 10, 20, false };
    QCOMPARE(range.clamp(0), qint64(0));
    QCOMPARE(range.clamp(999), qint64(999));
}

void TestPlaybackRange::clampConfinesFrameWhenEnabled()
{
    const PlaybackRange range{ 10, 20, true };
    QCOMPARE(range.clamp(0), qint64(10));
    QCOMPARE(range.clamp(15), qint64(15));
    QCOMPARE(range.clamp(999), qint64(20));
}

QTEST_GUILESS_MAIN(TestPlaybackRange)
#include "tst_playbackrange.moc"
