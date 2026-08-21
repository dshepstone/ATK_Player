#include "media/WaveformData.h"

#include <QTest>
#include <QVector>

using atk::media::WaveformData;
using atk::media::WaveformPeak;

namespace {

QVector<WaveformPeak> makePeaks(int count, float amplitude)
{
    QVector<WaveformPeak> peaks;
    peaks.reserve(count);
    for (int i = 0; i < count; ++i) {
        peaks.append(WaveformPeak{ -amplitude, amplitude });
    }
    return peaks;
}

} // namespace

/// Covers the waveform peak pyramid: how buckets map to media time, how levels
/// aggregate, and that a transient survives being folded upward.
class TestWaveformData : public QObject {
    Q_OBJECT

private slots:
    void startsEmpty();
    void appendingGrowsCoverage();
    void bucketDurationDoublesPerLevel();
    void aggregatesPairsIntoCoarserLevels();
    void transientSurvivesAggregation();
    void selectsLevelForPixelWidth();
    void mapsMediaTimeToBuckets();
    void peakOverRangeMergesBuckets();
    void outOfRangeLookupsAreSilent();
    void partialBucketsWaitForTheirPair();
    void sourceGenerationChangeClears();
    void memoryStaysCompactForLongMedia();
    void clearResets();

    void tracksPeakAmplitude();
    void normalisesQuietMaterial();
    void doesNotAmplifyNearSilence();
    void clearResetsPeak();
};

void TestWaveformData::startsEmpty()
{
    WaveformData data;
    QVERIFY(data.isEmpty());
    QCOMPARE(data.coveredUs(), qint64(0));
    QVERIFY(!data.isComplete());
}

void TestWaveformData::appendingGrowsCoverage()
{
    WaveformData data;
    data.appendBaseBuckets(makePeaks(100, 0.5f));

    QVERIFY(!data.isEmpty());
    QCOMPARE(data.coveredUs(), 100 * WaveformData::kBaseBucketUs);
    QCOMPARE(data.level(0).size(), 100);

    // A second chunk continues where the first stopped rather than replacing it.
    data.appendBaseBuckets(makePeaks(50, 0.5f));
    QCOMPARE(data.level(0).size(), 150);
    QCOMPARE(data.coveredUs(), 150 * WaveformData::kBaseBucketUs);
}

void TestWaveformData::bucketDurationDoublesPerLevel()
{
    QCOMPARE(WaveformData::bucketDurationUs(0), WaveformData::kBaseBucketUs);
    QCOMPARE(WaveformData::bucketDurationUs(1), WaveformData::kBaseBucketUs * 2);
    QCOMPARE(WaveformData::bucketDurationUs(3), WaveformData::kBaseBucketUs * 8);
}

void TestWaveformData::aggregatesPairsIntoCoarserLevels()
{
    WaveformData data;
    data.appendBaseBuckets(makePeaks(64, 0.5f));

    QCOMPARE(data.level(0).size(), 64);
    QCOMPARE(data.level(1).size(), 32);
    QCOMPARE(data.level(2).size(), 16);
    QCOMPARE(data.level(3).size(), 8);
}

void TestWaveformData::transientSurvivesAggregation()
{
    WaveformData data;

    // A single loud bucket among quiet ones -- an impact, or a plosive. The
    // whole reason buckets store min/max rather than an average is that this
    // must still be visible at coarse zoom.
    QVector<WaveformPeak> peaks = makePeaks(64, 0.05f);
    peaks[33] = WaveformPeak{ -0.9f, 0.9f };
    data.appendBaseBuckets(peaks);

    // Bucket 33 folds into 16 at level 1, 8 at level 2, 4 at level 3.
    QCOMPARE(data.level(1).at(16).maximum, 0.9f);
    QCOMPARE(data.level(2).at(8).maximum, 0.9f);
    QCOMPARE(data.level(3).at(4).maximum, 0.9f);

    // And a quiet neighbourhood stays quiet.
    QVERIFY(data.level(3).at(0).maximum < 0.1f);
}

void TestWaveformData::selectsLevelForPixelWidth()
{
    WaveformData data;
    data.appendBaseBuckets(makePeaks(4096, 0.5f));

    // Asking for the base duration or finer gives the finest level.
    QCOMPARE(data.levelForBucketDuration(WaveformData::kBaseBucketUs), 0);
    QCOMPARE(data.levelForBucketDuration(1000), 0);

    // A pixel covering exactly two base buckets selects level 1.
    QCOMPARE(data.levelForBucketDuration(WaveformData::kBaseBucketUs * 2), 1);
    QCOMPARE(data.levelForBucketDuration(WaveformData::kBaseBucketUs * 4), 2);

    // Never finer than requested, which is what bounds per-column work.
    const int level = data.levelForBucketDuration(WaveformData::kBaseBucketUs * 3);
    QVERIFY(WaveformData::bucketDurationUs(level) <= WaveformData::kBaseBucketUs * 3);
}

void TestWaveformData::mapsMediaTimeToBuckets()
{
    WaveformData data;
    QVector<WaveformPeak> peaks = makePeaks(100, 0.1f);
    peaks[50] = WaveformPeak{ -0.8f, 0.8f };
    data.appendBaseBuckets(peaks);

    // Bucket 50 covers [500 ms, 510 ms) at a 10 ms base.
    const int64_t inside = 50 * WaveformData::kBaseBucketUs + 1;
    QCOMPARE(data.peakAt(0, inside).maximum, 0.8f);

    // Its neighbours are quiet, so the mapping is not off by one.
    QVERIFY(data.peakAt(0, 49 * WaveformData::kBaseBucketUs).maximum < 0.2f);
    QVERIFY(data.peakAt(0, 51 * WaveformData::kBaseBucketUs).maximum < 0.2f);
}

void TestWaveformData::peakOverRangeMergesBuckets()
{
    WaveformData data;
    QVector<WaveformPeak> peaks = makePeaks(100, 0.1f);
    peaks[20] = WaveformPeak{ -0.7f, 0.7f };
    data.appendBaseBuckets(peaks);

    // A pixel column spanning buckets 18..24 must show the loud one inside it.
    const int64_t start = 18 * WaveformData::kBaseBucketUs;
    const int64_t end = 25 * WaveformData::kBaseBucketUs;
    QCOMPARE(data.peakOverRange(0, start, end).maximum, 0.7f);

    // A column that excludes it must not.
    const int64_t quietStart = 30 * WaveformData::kBaseBucketUs;
    const int64_t quietEnd = 35 * WaveformData::kBaseBucketUs;
    QVERIFY(data.peakOverRange(0, quietStart, quietEnd).maximum < 0.2f);
}

void TestWaveformData::outOfRangeLookupsAreSilent()
{
    WaveformData data;
    data.appendBaseBuckets(makePeaks(10, 0.5f));

    // Past the analysed region and before zero: silent rather than a crash, so
    // the renderer never needs to bounds-check while drawing.
    QVERIFY(data.peakAt(0, 100 * WaveformData::kBaseBucketUs).isSilent());
    QVERIFY(data.peakAt(0, -5000).isSilent());
    QVERIFY(data.peakOverRange(0, 1'000'000, 2'000'000).isSilent());
}

void TestWaveformData::partialBucketsWaitForTheirPair()
{
    WaveformData data;

    // An odd count leaves one level-0 bucket without a partner. Folding it
    // early would publish a coarse bucket at half coverage that becomes wrong
    // as soon as the next chunk arrives.
    data.appendBaseBuckets(makePeaks(7, 0.5f));
    QCOMPARE(data.level(0).size(), 7);
    QCOMPARE(data.level(1).size(), 3);

    data.appendBaseBuckets(makePeaks(1, 0.5f));
    QCOMPARE(data.level(0).size(), 8);
    QCOMPARE(data.level(1).size(), 4);
}

void TestWaveformData::sourceGenerationChangeClears()
{
    WaveformData data;
    data.setSourceGeneration(1);
    data.appendBaseBuckets(makePeaks(50, 0.5f));
    data.setComplete(true);
    QVERIFY(!data.isEmpty());

    // Peaks from the previous file must never be drawn under a new one.
    data.setSourceGeneration(2);
    QVERIFY(data.isEmpty());
    QVERIFY(!data.isComplete());
    QCOMPARE(data.sourceGeneration(), quint64(2));
}

void TestWaveformData::memoryStaysCompactForLongMedia()
{
    WaveformData data;

    // Two hours at a 10 ms base is 720,000 level-0 buckets. The pyramid roughly
    // doubles that, so the whole thing should stay in single-digit megabytes --
    // the property that makes analysing a feature-length file reasonable.
    constexpr int twoHoursOfBuckets = 720'000;
    data.appendBaseBuckets(makePeaks(twoHoursOfBuckets, 0.4f));

    QCOMPARE(data.coveredUs(), qint64(twoHoursOfBuckets) * WaveformData::kBaseBucketUs);

    const int64_t megabytes = data.memoryBytes() / (1024 * 1024);
    QVERIFY2(megabytes < 32,
             qPrintable(QStringLiteral("waveform used %1 MB for two hours").arg(megabytes)));
}

void TestWaveformData::clearResets()
{
    WaveformData data;
    data.appendBaseBuckets(makePeaks(20, 0.5f));
    data.setComplete(true);

    data.clear();
    QVERIFY(data.isEmpty());
    QVERIFY(!data.isComplete());
    QCOMPARE(data.coveredUs(), qint64(0));
}

void TestWaveformData::tracksPeakAmplitude()
{
    WaveformData data;
    data.appendBaseBuckets(makePeaks(10, 0.2f));
    QCOMPARE(data.peakAmplitude(), 0.2f);

    // The running peak must rise with later chunks, since analysis is
    // progressive and the renderer needs a gain from the first chunk onward.
    QVector<WaveformPeak> louder = makePeaks(5, 0.2f);
    louder[2] = WaveformPeak{ -0.75f, 0.75f };
    data.appendBaseBuckets(louder);
    QCOMPARE(data.peakAmplitude(), 0.75f);
}

void TestWaveformData::normalisesQuietMaterial()
{
    WaveformData data;

    // Dialogue mastered well below full scale. Drawn at true scale this is a
    // thin flat band; the gain is what makes it readable.
    data.appendBaseBuckets(makePeaks(50, 0.25f));

    const float gain = data.displayGain();
    QVERIFY2(gain > 3.0f, qPrintable(QStringLiteral("gain was %1").arg(gain)));

    // Scaling by the gain brings the loudest bucket to roughly full height
    // without exceeding it, so nothing clips off the top of the band.
    const float scaled = data.peakAmplitude() * gain;
    QVERIFY(scaled > 0.9f);
    QVERIFY(scaled <= 1.01f);
}

void TestWaveformData::doesNotAmplifyNearSilence()
{
    WaveformData data;

    // A genuinely near-silent file must keep looking quiet rather than being
    // amplified into a wall of visual noise.
    data.appendBaseBuckets(makePeaks(50, 0.01f));
    QCOMPARE(data.displayGain(), 1.0f);
}

void TestWaveformData::clearResetsPeak()
{
    WaveformData data;
    data.appendBaseBuckets(makePeaks(10, 0.8f));
    QVERIFY(data.peakAmplitude() > 0.5f);

    // A new source must not inherit the previous file's scaling.
    data.setSourceGeneration(2);
    QCOMPARE(data.peakAmplitude(), 0.0f);
    QCOMPARE(data.displayGain(), 1.0f);
}

QTEST_GUILESS_MAIN(TestWaveformData)
#include "tst_waveformdata.moc"
