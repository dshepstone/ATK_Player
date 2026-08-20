#include "media/ScrubAudioWorker.h"

#include "media/AudioBuffer.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>

using atk::media::ScrubAudioWorker;

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 1;
constexpr int64_t kGrainUs = 80'000;

/// Same fixture the audio review tests use: tones at known media positions.
constexpr int64_t kQuietToneStartUs = 500'000;
constexpr int64_t kLoudToneStartUs = 1'500'000;

QString reviewFixture()
{
    return QDir(QString::fromUtf8(ATK_TEST_MEDIA_DIR))
        .filePath(QStringLiteral("atk_review_10s.mkv"));
}

float peakAmplitude(const QByteArray& pcm)
{
    const auto* samples = reinterpret_cast<const int16_t*>(pcm.constData());
    const qsizetype count = pcm.size() / 2;
    int32_t peak = 0;
    for (qsizetype i = 0; i < count; ++i) {
        peak = std::max<int32_t>(peak, std::abs(static_cast<int32_t>(samples[i])));
    }
    return static_cast<float>(peak) / 32768.0f;
}

} // namespace

/// Covers grain extraction and request coalescing.
///
/// Runs the worker directly rather than through a QAudioSink, so it needs no
/// audio device and is deterministic in CI. What it cannot check is whether the
/// result *sounds* useful for review -- that needs a person.
class TestScrubAudio : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void deliversGrainForRequestedPosition();
    void grainContentMatchesThePosition();
    void grainIsRoughlyTheRequestedLength();
    void dropsRequestsOlderThanTheNewest();
    void newestRequestStillDelivers();
    void repeatedPositionsUseTheCache();
    void refusesGrainsFromAnotherSource();
    void unavailableAudioIsReportedNotFatal();
};

void TestScrubAudio::initTestCase()
{
    if (!QFileInfo::exists(reviewFixture())) {
        QSKIP("Review fixture not found; build the atk_test_media target first.");
    }
    qRegisterMetaType<quint64>("quint64");
}

void TestScrubAudio::deliversGrainForRequestedPosition()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);

    worker.openSource(reviewFixture(), kSampleRate, kChannels, 1);
    worker.requestGrain(kLoudToneStartUs + 100'000, kGrainUs, 1, 1);

    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.at(0);
    const auto actualStartUs = args.at(2).toLongLong();

    // Snapped to the 20 ms grain grid, which stays well inside a frame at
    // 24 fps so the alignment is never audible as a timing error.
    const int64_t requested = kLoudToneStartUs + 100'000;
    QVERIFY2(std::abs(actualStartUs - requested) <= ScrubAudioWorker::kGrainAlignUs,
             qPrintable(QStringLiteral("grain started %1 us from the request")
                            .arg(actualStartUs - requested)));
}

void TestScrubAudio::grainContentMatchesThePosition()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);
    worker.openSource(reviewFixture(), kSampleRate, kChannels, 1);

    // A position the fixture makes loud, and one it makes silent. This is the
    // property that matters for review: what you hear belongs to where you are
    // pointing.
    worker.requestGrain(kLoudToneStartUs + 200'000, kGrainUs, 1, 1);
    QCOMPARE(spy.count(), 1);
    QVERIFY(peakAmplitude(spy.at(0).at(0).toByteArray()) > 0.025f);

    spy.clear();
    worker.requestGrain(5'000'000, kGrainUs, 2, 1);
    QCOMPARE(spy.count(), 1);
    QVERIFY(peakAmplitude(spy.at(0).at(0).toByteArray()) < 0.02f);
}

void TestScrubAudio::grainIsRoughlyTheRequestedLength()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);
    worker.openSource(reviewFixture(), kSampleRate, kChannels, 1);
    worker.requestGrain(2'000'000, kGrainUs, 1, 1);

    QCOMPARE(spy.count(), 1);

    atk::media::AudioFormat format;
    format.sampleRate = kSampleRate;
    format.channelCount = kChannels;
    format.bytesPerSample = 2;

    const QByteArray pcm = spy.at(0).at(0).toByteArray();
    const int64_t durationUs = format.bytesToMicroseconds(pcm.size());
    QVERIFY2(std::abs(durationUs - kGrainUs) < 10'000,
             qPrintable(QStringLiteral("grain was %1 us").arg(durationUs)));
}

void TestScrubAudio::dropsRequestsOlderThanTheNewest()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);
    worker.openSource(reviewFixture(), kSampleRate, kChannels, 1);

    // Dragging fast queues several positions. Only the newest is worth
    // decoding; the rest describe places the pointer has already left.
    worker.requestGrain(3'000'000, kGrainUs, 10, 1);
    QCOMPARE(spy.count(), 1);

    spy.clear();
    worker.requestGrain(1'000'000, kGrainUs, 5, 1); // older sequence
    QCOMPARE(spy.count(), 0);
}

void TestScrubAudio::newestRequestStillDelivers()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);
    worker.openSource(reviewFixture(), kSampleRate, kChannels, 1);

    // Coalescing must not silence scrubbing altogether: each newer request is
    // still served.
    for (quint64 sequence = 1; sequence <= 5; ++sequence) {
        worker.requestGrain(1'000'000 + static_cast<qint64>(sequence) * 100'000,
                            kGrainUs, sequence, 1);
    }
    QCOMPARE(spy.count(), 5);
}

void TestScrubAudio::repeatedPositionsUseTheCache()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);
    worker.openSource(reviewFixture(), kSampleRate, kChannels, 1);

    // Review scrubbing goes back and forth over the same moment repeatedly, so
    // the same position must give the same audio without re-decoding.
    worker.requestGrain(kQuietToneStartUs + 200'000, kGrainUs, 1, 1);
    QCOMPARE(spy.count(), 1);
    const QByteArray first = spy.at(0).at(0).toByteArray();

    spy.clear();
    worker.requestGrain(kQuietToneStartUs + 200'000, kGrainUs, 2, 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toByteArray(), first);
}

void TestScrubAudio::refusesGrainsFromAnotherSource()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);
    worker.openSource(reviewFixture(), kSampleRate, kChannels, 7);

    // A request tagged with a file that is no longer open must produce nothing,
    // or audio from the previous clip would be heard under the new one.
    worker.requestGrain(1'000'000, kGrainUs, 1, 6);
    QCOMPARE(spy.count(), 0);

    worker.requestGrain(1'000'000, kGrainUs, 2, 7);
    QCOMPARE(spy.count(), 1);
}

void TestScrubAudio::unavailableAudioIsReportedNotFatal()
{
    ScrubAudioWorker worker;
    QSignalSpy unavailable(&worker, &ScrubAudioWorker::sourceUnavailable);
    QSignalSpy grains(&worker, &ScrubAudioWorker::grainReady);

    worker.openSource(QStringLiteral("C:/definitely/not/here_c3d4.mkv"),
                      kSampleRate, kChannels, 1);
    QCOMPARE(unavailable.count(), 1);

    // Requests afterwards are ignored quietly; visual scrubbing carries on.
    worker.requestGrain(1'000'000, kGrainUs, 1, 1);
    QCOMPARE(grains.count(), 0);
}

QTEST_GUILESS_MAIN(TestScrubAudio)
#include "tst_scrubaudio.moc"
