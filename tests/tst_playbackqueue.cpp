#include "media/PlaybackQueue.h"

#include <QTest>

using atk::media::PlaybackQueue;
using atk::media::VideoFrame;

namespace {

/// A frame of a given size, so budget behaviour can be exercised without
/// decoding anything.
VideoFrame makeFrame(int64_t index, int width = 64, int height = 64,
                     uint64_t sourceGeneration = 0)
{
    VideoFrame frame;
    frame.frameIndex = index;
    frame.sourceGeneration = sourceGeneration;
    frame.width = width;
    frame.height = height;
    frame.ptsUs = index * 1000;
    frame.image = QImage(width, height, QImage::Format_RGB32);
    frame.image.fill(Qt::black);
    return frame;
}

} // namespace

/// Covers the buffer that feeds real-time playback.
///
/// Playback originally read straight from the LRU FrameCache, which evicted its
/// own lookahead at 1080p and above. These tests pin the properties that made a
/// separate structure necessary: presentation order, a hard bound, and never
/// discarding the frame that is about to be shown.
class TestPlaybackQueue : public QObject {
    Q_OBJECT

private slots:
    void startsEmpty();
    void storesAndFindsFrames();
    void keepsPresentationOrder();
    void discardsUpToPresentedFrame();
    void discardUpToLeavesTheFutureIntact();
    void refusesForeignSourceGeneration();
    void changingSourceGenerationClears();
    void boundsMemoryUse();
    void dropsNewestWhenOverBudget();
    void replacingAFrameDoesNotDoubleCount();
    void clearResetsUsage();
};

void TestPlaybackQueue::startsEmpty()
{
    PlaybackQueue queue;
    QVERIFY(queue.isEmpty());
    QCOMPARE(queue.count(), std::size_t(0));
    QCOMPARE(queue.usedBytes(), qint64(0));
    QCOMPARE(queue.firstFrame(), qint64(-1));
    QCOMPARE(queue.lastFrame(), qint64(-1));
}

void TestPlaybackQueue::storesAndFindsFrames()
{
    PlaybackQueue queue;
    queue.insert(makeFrame(10));

    const VideoFrame* found = queue.find(10);
    QVERIFY(found != nullptr);
    QCOMPARE(found->frameIndex, qint64(10));
    QVERIFY(queue.find(11) == nullptr);
}

void TestPlaybackQueue::keepsPresentationOrder()
{
    PlaybackQueue queue;

    // Inserted out of order, as a decoder emitting reordered B-frames would.
    for (const int64_t index : { 5, 3, 9, 1, 7 }) {
        queue.insert(makeFrame(index));
    }

    // The queue reports its extent in presentation order regardless.
    QCOMPARE(queue.firstFrame(), qint64(1));
    QCOMPARE(queue.lastFrame(), qint64(9));
    QCOMPARE(queue.count(), std::size_t(5));
}

void TestPlaybackQueue::discardsUpToPresentedFrame()
{
    PlaybackQueue queue;
    for (int64_t index = 0; index < 10; ++index) {
        queue.insert(makeFrame(index));
    }

    // Presenting frame 4 makes 0..3 past.
    QCOMPARE(queue.discardUpTo(4), 4);
    QCOMPARE(queue.firstFrame(), qint64(4));
    QVERIFY(queue.find(3) == nullptr);
    QVERIFY(queue.find(4) != nullptr);
}

void TestPlaybackQueue::discardUpToLeavesTheFutureIntact()
{
    PlaybackQueue queue;
    for (int64_t index = 0; index < 6; ++index) {
        queue.insert(makeFrame(index));
    }

    queue.discardUpTo(2);

    // Everything from the presented frame onward must survive -- that is the
    // lookahead playback is about to need.
    for (int64_t index = 2; index < 6; ++index) {
        QVERIFY2(queue.find(index) != nullptr,
                 qPrintable(QStringLiteral("frame %1 was discarded").arg(index)));
    }
}

void TestPlaybackQueue::refusesForeignSourceGeneration()
{
    PlaybackQueue queue;
    queue.setSourceGeneration(3);

    // A frame from a file that is no longer open must not become presentable.
    queue.insert(makeFrame(1, 64, 64, /*sourceGeneration=*/2));
    QCOMPARE(queue.count(), std::size_t(0));

    queue.insert(makeFrame(1, 64, 64, /*sourceGeneration=*/3));
    QCOMPARE(queue.count(), std::size_t(1));
}

void TestPlaybackQueue::changingSourceGenerationClears()
{
    PlaybackQueue queue;
    queue.setSourceGeneration(1);
    queue.insert(makeFrame(1, 64, 64, 1));
    queue.insert(makeFrame(2, 64, 64, 1));
    QCOMPARE(queue.count(), std::size_t(2));

    queue.setSourceGeneration(2);
    QCOMPARE(queue.count(), std::size_t(0));
    QCOMPARE(queue.usedBytes(), qint64(0));
}

void TestPlaybackQueue::boundsMemoryUse()
{
    PlaybackQueue queue;

    // 64x64 RGB32 is 16 KiB; a 64 KiB budget therefore holds about four.
    queue.setBudgetBytes(64 * 1024);

    for (int64_t index = 0; index < 50; ++index) {
        queue.insert(makeFrame(index));
    }

    // The whole point is that a long clip cannot grow the queue without limit.
    QVERIFY2(queue.usedBytes() <= queue.budgetBytes(),
             qPrintable(QStringLiteral("used %1 > budget %2")
                            .arg(queue.usedBytes()).arg(queue.budgetBytes())));
    QVERIFY(queue.count() < 50);
}

void TestPlaybackQueue::dropsNewestWhenOverBudget()
{
    PlaybackQueue queue;
    queue.setBudgetBytes(64 * 1024);

    for (int64_t index = 0; index < 20; ++index) {
        queue.insert(makeFrame(index));
    }

    // Frame 0 is the one about to be presented. Trimming must take from the far
    // end instead -- dropping the front would be a visible skip, whereas
    // dropping the furthest-future frame only costs a re-decode.
    QVERIFY2(queue.find(0) != nullptr, "the frame about to be presented was dropped");
    QCOMPARE(queue.firstFrame(), qint64(0));
    QVERIFY(queue.lastFrame() < 19);
}

void TestPlaybackQueue::replacingAFrameDoesNotDoubleCount()
{
    PlaybackQueue queue;
    queue.insert(makeFrame(7));
    const int64_t afterFirst = queue.usedBytes();

    queue.insert(makeFrame(7));
    QCOMPARE(queue.count(), std::size_t(1));
    QCOMPARE(queue.usedBytes(), afterFirst);
}

void TestPlaybackQueue::clearResetsUsage()
{
    PlaybackQueue queue;
    for (int64_t index = 0; index < 5; ++index) {
        queue.insert(makeFrame(index));
    }
    QVERIFY(queue.usedBytes() > 0);

    queue.clear();
    QCOMPARE(queue.usedBytes(), qint64(0));
    QVERIFY(queue.isEmpty());
}

QTEST_GUILESS_MAIN(TestPlaybackQueue)
#include "tst_playbackqueue.moc"
