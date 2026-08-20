#include "media/FrameCache.h"

#include <QTest>

using atk::media::FrameCache;
using atk::media::VideoFrame;

namespace {

/// A frame whose backing image is a known size, so byte budgets are predictable.
VideoFrame makeFrame(qint64 number, int width = 64, int height = 64)
{
    VideoFrame frame;
    frame.frameIndex = number;
    frame.ptsUs = number * 1000;
    frame.width = width;
    frame.height = height;
    frame.image = QImage(width, height, QImage::Format_ARGB32);
    frame.image.fill(Qt::black);
    return frame;
}

qsizetype frameBytes(int width = 64, int height = 64)
{
    return makeFrame(0, width, height).sizeInBytes();
}

} // namespace

class TestFrameCache : public QObject {
    Q_OBJECT

private slots:
    void missesOnEmptyCache();
    void storesAndRetrieves();
    void replacesFrameWithSameNumber();
    void evictsLeastRecentlyUsed();
    void findPromotesEntry();
    void rejectsFrameLargerThanBudget();
    void ignoresInvalidFrames();
    void clearResetsUsage();
    void shrinkingBudgetEvicts();
};

void TestFrameCache::missesOnEmptyCache()
{
    FrameCache cache;
    QVERIFY(cache.find(0) == nullptr);
    QCOMPARE(cache.count(), std::size_t(0));
    QCOMPARE(cache.usedBytes(), qint64(0));
}

void TestFrameCache::storesAndRetrieves()
{
    FrameCache cache;
    cache.insert(makeFrame(7));

    const VideoFrame* found = cache.find(7);
    QVERIFY(found != nullptr);
    QCOMPARE(found->frameIndex, qint64(7));
    QCOMPARE(cache.count(), std::size_t(1));
    QCOMPARE(cache.usedBytes(), qint64(frameBytes()));
}

void TestFrameCache::replacesFrameWithSameNumber()
{
    FrameCache cache;
    cache.insert(makeFrame(3));
    cache.insert(makeFrame(3));

    QCOMPARE(cache.count(), std::size_t(1));
    // Re-inserting must not double-count the bytes.
    QCOMPARE(cache.usedBytes(), qint64(frameBytes()));
}

void TestFrameCache::evictsLeastRecentlyUsed()
{
    // Budget for exactly two frames.
    FrameCache cache(frameBytes() * 2);

    cache.insert(makeFrame(1));
    cache.insert(makeFrame(2));
    cache.insert(makeFrame(3));

    QCOMPARE(cache.count(), std::size_t(2));
    QVERIFY(!cache.contains(1));
    QVERIFY(cache.contains(2));
    QVERIFY(cache.contains(3));
}

void TestFrameCache::findPromotesEntry()
{
    FrameCache cache(frameBytes() * 2);

    cache.insert(makeFrame(1));
    cache.insert(makeFrame(2));

    // Touching frame 1 makes frame 2 the eviction candidate.
    QVERIFY(cache.find(1) != nullptr);
    cache.insert(makeFrame(3));

    QVERIFY(cache.contains(1));
    QVERIFY(!cache.contains(2));
    QVERIFY(cache.contains(3));
}

void TestFrameCache::rejectsFrameLargerThanBudget()
{
    FrameCache cache(frameBytes() / 2);
    cache.insert(makeFrame(1));

    // Caching it would evict everything and then drop it anyway.
    QCOMPARE(cache.count(), std::size_t(0));
    QCOMPARE(cache.usedBytes(), qint64(0));
}

void TestFrameCache::ignoresInvalidFrames()
{
    FrameCache cache;
    cache.insert(VideoFrame{});
    QCOMPARE(cache.count(), std::size_t(0));
}

void TestFrameCache::clearResetsUsage()
{
    FrameCache cache;
    cache.insert(makeFrame(1));
    cache.insert(makeFrame(2));
    cache.clear();

    QCOMPARE(cache.count(), std::size_t(0));
    QCOMPARE(cache.usedBytes(), qint64(0));
}

void TestFrameCache::shrinkingBudgetEvicts()
{
    FrameCache cache(frameBytes() * 4);
    cache.insert(makeFrame(1));
    cache.insert(makeFrame(2));
    cache.insert(makeFrame(3));
    QCOMPARE(cache.count(), std::size_t(3));

    cache.setBudgetBytes(frameBytes());
    QCOMPARE(cache.count(), std::size_t(1));
    QVERIFY(cache.contains(3));
}

QTEST_GUILESS_MAIN(TestFrameCache)
#include "tst_framecache.moc"
