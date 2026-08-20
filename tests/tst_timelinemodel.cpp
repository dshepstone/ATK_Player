#include "timeline/TimelineModel.h"

#include <QSignalSpy>
#include <QTest>

using atk::media::FrameRate;
using atk::timeline::Bookmark;
using atk::timeline::PlaybackRange;
using atk::timeline::TimelineModel;

class TestTimelineModel : public QObject {
    Q_OBJECT

private slots:
    void startsEmpty();
    void clampsPlayheadToExtent();
    void emitsCurrentFrameChangedOnlyOnChange();
    void rangeConfinesPlayhead();
    void setRangeInFromCurrentFrame();
    void setRangeOutFromCurrentFrame();
    void normalisesReversedRange();
    void clampsRangeToExtent();
    void normalisesNegativeRange();
    void frameCountChangesKeepStateValid();
    void effectiveBoundsFollowRange();
    void bookmarksStaySortedAndUnique();
    void bookmarkNavigation();
    void resetClearsEverything();
};

void TestTimelineModel::startsEmpty()
{
    TimelineModel model;
    QCOMPARE(model.frameCount(), qint64(0));
    QCOMPARE(model.currentFrame(), qint64(0));
    QCOMPARE(model.lastFrame(), qint64(-1));
    QVERIFY(!model.frameRate().isValid());
    QVERIFY(model.bookmarks().isEmpty());
}

void TestTimelineModel::clampsPlayheadToExtent()
{
    TimelineModel model;
    model.setFrameCount(100);

    model.setCurrentFrame(500);
    QCOMPARE(model.currentFrame(), qint64(99));

    model.setCurrentFrame(-20);
    QCOMPARE(model.currentFrame(), qint64(0));
}

void TestTimelineModel::emitsCurrentFrameChangedOnlyOnChange()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setCurrentFrame(10);

    QSignalSpy spy(&model, &TimelineModel::currentFrameChanged);
    model.setCurrentFrame(10);
    QCOMPARE(spy.count(), 0);

    model.setCurrentFrame(11);
    QCOMPARE(spy.count(), 1);
}

void TestTimelineModel::rangeConfinesPlayhead()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setCurrentFrame(90);

    model.setPlaybackRange(PlaybackRange{ 10, 20, true });

    // Setting the range must pull the playhead inside it immediately.
    QCOMPARE(model.currentFrame(), qint64(20));

    model.setCurrentFrame(0);
    QCOMPARE(model.currentFrame(), qint64(10));
}

void TestTimelineModel::setRangeInFromCurrentFrame()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setCurrentFrame(30);

    model.setRangeInAtCurrentFrame();

    QVERIFY(model.playbackRange().enabled);
    QCOMPARE(model.playbackRange().startFrame, qint64(30));
    // With no range set beforehand, the out point runs to the end.
    QCOMPARE(model.playbackRange().endFrame, qint64(99));
}

void TestTimelineModel::setRangeOutFromCurrentFrame()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setCurrentFrame(40);

    model.setRangeOutAtCurrentFrame();

    QVERIFY(model.playbackRange().enabled);
    QCOMPARE(model.playbackRange().startFrame, qint64(0));
    QCOMPARE(model.playbackRange().endFrame, qint64(40));
}

void TestTimelineModel::normalisesReversedRange()
{
    TimelineModel model;
    model.setFrameCount(100);

    model.setPlaybackRange(PlaybackRange{ 60, 20, true });

    QCOMPARE(model.playbackRange().startFrame, qint64(20));
    QCOMPARE(model.playbackRange().endFrame, qint64(60));
}

void TestTimelineModel::clampsRangeToExtent()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setPlaybackRange(PlaybackRange{ 90, 500, true });

    QCOMPARE(model.playbackRange().startFrame, qint64(90));
    QCOMPARE(model.playbackRange().endFrame, qint64(99));
}

void TestTimelineModel::normalisesNegativeRange()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setPlaybackRange(PlaybackRange{ -20, -10, true });

    QCOMPARE(model.playbackRange().startFrame, qint64(0));
    QCOMPARE(model.playbackRange().endFrame, qint64(0));
    QVERIFY(model.playbackRange().isValid());
}

void TestTimelineModel::frameCountChangesKeepStateValid()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setCurrentFrame(90);
    model.setPlaybackRange(PlaybackRange{ 80, 99, true });

    model.setFrameCount(1);
    QCOMPARE(model.currentFrame(), qint64(0));
    QCOMPARE(model.playbackRange().startFrame, qint64(0));
    QCOMPARE(model.playbackRange().endFrame, qint64(0));

    model.setFrameCount(0);
    QCOMPARE(model.currentFrame(), qint64(0));
    QCOMPARE(model.lastFrame(), qint64(-1));
}

void TestTimelineModel::effectiveBoundsFollowRange()
{
    TimelineModel model;
    model.setFrameCount(100);

    QCOMPARE(model.effectiveStartFrame(), qint64(0));
    QCOMPARE(model.effectiveEndFrame(), qint64(99));

    model.setPlaybackRange(PlaybackRange{ 10, 20, true });
    QCOMPARE(model.effectiveStartFrame(), qint64(10));
    QCOMPARE(model.effectiveEndFrame(), qint64(20));

    model.clearPlaybackRange();
    QCOMPARE(model.effectiveStartFrame(), qint64(0));
    QCOMPARE(model.effectiveEndFrame(), qint64(99));
}

void TestTimelineModel::bookmarksStaySortedAndUnique()
{
    TimelineModel model;
    model.setFrameCount(100);

    Bookmark first;
    first.frame = 50;
    Bookmark second;
    second.frame = 10;
    Bookmark replacement;
    replacement.frame = 50;
    replacement.name = QStringLiteral("contact");

    model.addBookmark(first);
    model.addBookmark(second);
    model.addBookmark(replacement);

    QCOMPARE(model.bookmarks().size(), qsizetype(2));
    QCOMPARE(model.bookmarks().at(0).frame, qint64(10));
    QCOMPARE(model.bookmarks().at(1).frame, qint64(50));
    // The later bookmark on frame 50 replaced the earlier one.
    QCOMPARE(model.bookmarks().at(1).name, QStringLiteral("contact"));
}

void TestTimelineModel::bookmarkNavigation()
{
    TimelineModel model;
    model.setFrameCount(100);

    for (const qint64 frame : { qint64(10), qint64(30), qint64(70) }) {
        Bookmark bookmark;
        bookmark.frame = frame;
        model.addBookmark(bookmark);
    }

    QCOMPARE(model.nextBookmarkFrame(0), qint64(10));
    QCOMPARE(model.nextBookmarkFrame(10), qint64(30));
    QCOMPARE(model.nextBookmarkFrame(70), qint64(-1));

    QCOMPARE(model.previousBookmarkFrame(99), qint64(70));
    QCOMPARE(model.previousBookmarkFrame(30), qint64(10));
    QCOMPARE(model.previousBookmarkFrame(10), qint64(-1));

    QVERIFY(model.bookmarkAt(30) != nullptr);
    QVERIFY(model.bookmarkAt(31) == nullptr);
}

void TestTimelineModel::resetClearsEverything()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setFrameRate(FrameRate::fromInteger(24));
    model.setCurrentFrame(50);
    model.setPlaybackRange(PlaybackRange{ 10, 60, true });

    Bookmark bookmark;
    bookmark.frame = 20;
    model.addBookmark(bookmark);

    model.reset();

    QCOMPARE(model.frameCount(), qint64(0));
    QCOMPARE(model.currentFrame(), qint64(0));
    QVERIFY(model.bookmarks().isEmpty());
    QVERIFY(!model.playbackRange().enabled);
    QVERIFY(!model.frameRate().isValid());
}

QTEST_GUILESS_MAIN(TestTimelineModel)
#include "tst_timelinemodel.moc"
