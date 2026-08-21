#include "timeline/TimelineModel.h"

#include <QSignalSpy>
#include <QTest>

using atk::media::FrameRate;
using atk::timeline::Bookmark;
using atk::timeline::BookmarkType;
using atk::timeline::PlaybackRange;
using atk::timeline::TimelineModel;

class TestTimelineModel : public QObject {
    Q_OBJECT

private slots:
    void startsEmpty();
    void clampsPlayheadToExtent();
    void emitsCurrentFrameChangedOnlyOnChange();
    void reviewRangeDoesNotMoveStoppedPlayhead();
    void setRangeInFromCurrentFrame();
    void setRangeOutFromCurrentFrame();
    void normalisesReversedRange();
    void clampsRangeToExtent();
    void normalisesNegativeRange();
    void frameCountChangesKeepStateValid();
    void effectiveBoundsFollowRange();
    void bookmarksStaySortedAndUnique();
    void bookmarkNavigation();
    void bookmarkIdsDeleteAndSourceReset();
    void rangeBookmarksValidateEditAndOrder();
    void bookmarkMetadataEditsKeepStableIdentity();
    void resetClearsEverything();
    void viewportZoomIsAnchorStableAndClamped();
    void viewportPansAndFollowsPlayhead();
    void viewportMappingIsDeterministic();
    void viewportRangeEditsAnchorAndClamp();
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

void TestTimelineModel::reviewRangeDoesNotMoveStoppedPlayhead()
{
    TimelineModel model;
    model.setFrameCount(100);
    model.setCurrentFrame(90);

    model.setPlaybackRange(PlaybackRange{ 10, 20, true });

    // View/range edits are UI state and must not seek stopped media.
    QCOMPARE(model.currentFrame(), qint64(90));

    model.setCurrentFrame(0);
    QCOMPARE(model.currentFrame(), qint64(0));
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
    // Adding again is a no-op: the stable marker is not replaced.
    QVERIFY(model.bookmarks().at(1).id != 0);
    QVERIFY(model.bookmarks().at(1).name.startsWith(QStringLiteral("Bookmark ")));
    QCOMPARE(model.bookmarks().at(1).mediaTimeUs, qint64(0)); // no rate set
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
    QCOMPARE(model.nextBookmarkFrame(70), qint64(10));

    QCOMPARE(model.previousBookmarkFrame(99), qint64(70));
    QCOMPARE(model.previousBookmarkFrame(30), qint64(10));
    QCOMPARE(model.previousBookmarkFrame(10), qint64(70));

    QVERIFY(model.bookmarkAt(30) != nullptr);
    QVERIFY(model.bookmarkAt(31) == nullptr);
}

void TestTimelineModel::bookmarkIdsDeleteAndSourceReset()
{
    TimelineModel model;
    model.setFrameRate(FrameRate{24000, 1001});
    model.setFrameCount(100);
    Bookmark bookmark;
    bookmark.frame = 24;
    model.addBookmark(bookmark);
    QCOMPARE(model.bookmarks().size(), qsizetype(1));
    const quint64 id = model.bookmarks().front().id;
    QVERIFY(id != 0);
    QCOMPARE(model.bookmarks().front().mediaTimeUs, model.mediaTimeForFrame(24));
    model.addBookmark(bookmark);
    QCOMPARE(model.bookmarks().front().id, id);
    model.removeBookmark(id);
    QVERIFY(model.bookmarks().isEmpty());

    model.addBookmark(bookmark);
    model.reset(); // source replacement/close boundary
    QVERIFY(model.bookmarks().isEmpty());
}

void TestTimelineModel::rangeBookmarksValidateEditAndOrder()
{
    TimelineModel model;
    model.setFrameRate(FrameRate{24000, 1001});
    model.setFrameCount(250);
    Bookmark range;
    range.type = BookmarkType::Range;
    range.frame = 138;
    range.endFrame = 300;
    const quint64 rangeId = model.addBookmark(range);
    const Bookmark* stored = model.bookmark(rangeId);
    QVERIFY(stored && stored->isRange());
    QCOMPARE(stored->frame, qint64(138));
    QCOMPARE(stored->endFrame, qint64(249));
    QCOMPARE(stored->frameLabel(), QStringLiteral("139–250"));

    Bookmark point;
    point.frame = 50;
    point.endFrame = 50;
    const quint64 pointId = model.addBookmark(point);
    QCOMPARE(model.bookmarks().front().id, pointId);

    Bookmark edited = *model.bookmark(rangeId);
    edited.frame = 100;
    edited.endFrame = 120;
    QVERIFY(model.updateBookmark(edited));
    QCOMPARE(model.bookmark(rangeId)->mediaTimeUs, model.mediaTimeForFrame(100));
    edited.frame = 121;
    edited.endFrame = 120;
    QVERIFY(!model.updateBookmark(edited));
    QCOMPARE(model.bookmark(rangeId)->frame, qint64(100));

    edited = *model.bookmark(rangeId);
    edited.endFrame = edited.frame;
    QVERIFY(model.updateBookmark(edited));
    QVERIFY(!model.bookmark(rangeId)->isRange());
    QCOMPARE(model.bookmark(rangeId)->id, rangeId);
}

void TestTimelineModel::bookmarkMetadataEditsKeepStableIdentity()
{
    TimelineModel model;
    model.setFrameCount(200);
    Bookmark bookmark;
    bookmark.frame = bookmark.endFrame = 20;
    const quint64 id = model.addBookmark(bookmark);
    QSignalSpy viewport(&model, &TimelineModel::viewportChanged);
    QSignalSpy playhead(&model, &TimelineModel::currentFrameChanged);
    Bookmark edited = *model.bookmark(id);
    edited.name = QStringLiteral("Contact");
    edited.note = QStringLiteral("Push silhouette\nWatch wrist");
    edited.colorIndex = 4;
    QVERIFY(model.updateBookmark(edited));
    QCOMPARE(model.bookmark(id)->id, id);
    QCOMPARE(model.bookmark(id)->name, edited.name);
    QCOMPARE(model.bookmark(id)->note, edited.note);
    QCOMPARE(model.bookmark(id)->colorIndex, 4);
    QCOMPARE(viewport.count(), 0);
    QCOMPARE(playhead.count(), 0);
    model.removeBookmark(id);
    QVERIFY(model.bookmarks().isEmpty());
}

void TestTimelineModel::viewportRangeEditsAnchorAndClamp()
{
    TimelineModel model;
    model.setFrameCount(1000);
    model.setViewportRange(100, 199);
    QCOMPARE(model.viewport().startFrame(), qint64(100));
    QCOMPARE(model.viewport().endFrame(), qint64(199));

    model.setViewportRange(190, 199);
    QCOMPARE(model.viewport().visibleFrameCount(), qint64(10));
    model.setViewportRange(198, 199); // cannot cross/minimise below ten
    QCOMPARE(model.viewport().visibleFrameCount(), qint64(10));
    QCOMPARE(model.viewport().endFrame(), qint64(207));

    model.setViewportRange(-50, 49);
    QCOMPARE(model.viewport().startFrame(), qint64(0));
    model.setViewportRange(950, 1100);
    QCOMPARE(model.viewport().endFrame(), qint64(999));
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

void TestTimelineModel::viewportZoomIsAnchorStableAndClamped()
{
    TimelineModel model;
    model.setFrameCount(1000);
    QCOMPARE(model.viewport().startFrame(), qint64(0));
    QCOMPARE(model.viewport().endFrame(), qint64(999));

    const double anchorFraction = model.viewport().fractionForFrame(250);
    model.zoomViewport(2.0, 250);
    QCOMPARE(model.viewport().visibleFrameCount(), qint64(500));
    QVERIFY(qAbs(model.viewport().fractionForFrame(250) - anchorFraction) < 0.003);

    for (int i = 0; i < 30; ++i) model.zoomViewport(2.0, 250);
    QCOMPARE(model.viewport().visibleFrameCount(), qint64(10));
    model.fitViewport();
    QCOMPARE(model.viewport().visibleFrameCount(), qint64(1000));
}

void TestTimelineModel::viewportPansAndFollowsPlayhead()
{
    TimelineModel model;
    model.setFrameCount(1000);
    model.zoomViewport(5.0, 500);
    const qint64 span = model.viewport().visibleFrameCount();
    model.panViewport(10000);
    QCOMPARE(model.viewport().endFrame(), qint64(999));
    QCOMPARE(model.viewport().visibleFrameCount(), span);
    model.ensureFrameVisible(0);
    QVERIFY(model.viewport().contains(0));
}

void TestTimelineModel::viewportMappingIsDeterministic()
{
    TimelineModel model;
    model.setFrameCount(101);
    model.zoomViewport(2.0, 50);
    QCOMPARE(model.viewport().frameAtFraction(0.0), model.viewport().startFrame());
    QCOMPARE(model.viewport().frameAtFraction(1.0), model.viewport().endFrame());
    const qint64 frame = model.viewport().frameAtFraction(0.37);
    QVERIFY(qAbs(model.viewport().fractionForFrame(frame) - 0.37) <=
            1.0 / double(model.viewport().visibleFrameCount() - 1));
}

QTEST_GUILESS_MAIN(TestTimelineModel)
#include "tst_timelinemodel.moc"
