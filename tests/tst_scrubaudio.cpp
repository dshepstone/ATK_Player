#include "media/ScrubAudioWorker.h"

#include "audio/ScrubAudioEngine.h"
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

    void grainIsCentredOnTheRequestedPosition();
    void reversesSampleFramesNotBytes();
    void reversalPreservesChannelOrder();
    void reversalIsItsOwnInverse();
    void refusesToReverseATrivialBuffer();
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

    // Grains are centred on the request, so the position under the pointer sits
    // in the middle of what is heard. The audible centre is what must line up;
    // it is snapped to the 20 ms grid, which stays well inside a frame at
    // 24 fps so the alignment is never audible as a timing error.
    const int64_t requested = kLoudToneStartUs + 100'000;
    const int64_t centreUs = actualStartUs + kGrainUs / 2;
    QVERIFY2(std::abs(centreUs - requested) <= ScrubAudioWorker::kGrainAlignUs,
             qPrintable(QStringLiteral("grain centre was %1 us from the request")
                            .arg(centreUs - requested)));
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

void TestScrubAudio::grainIsCentredOnTheRequestedPosition()
{
    ScrubAudioWorker worker;
    QSignalSpy spy(&worker, &ScrubAudioWorker::grainReady);
    worker.openSource(reviewFixture(), kSampleRate, kChannels, 1);

    const qint64 requested = 4'000'000;
    worker.requestGrain(requested, kGrainUs, 1, 1);
    QCOMPARE(spy.count(), 1);

    const auto actualStartUs = spy.at(0).at(2).toLongLong();

    // The grain starts about half its length before the request, so its audible
    // middle sits on the frame being pointed at. A start-aligned grain would
    // make every position sound half a grain late.
    const qint64 expectedStart = requested - kGrainUs / 2;
    QVERIFY2(std::abs(actualStartUs - expectedStart) <= ScrubAudioWorker::kGrainAlignUs,
             qPrintable(QStringLiteral("grain started at %1, expected near %2")
                            .arg(actualStartUs).arg(expectedStart)));
}

void TestScrubAudio::reversesSampleFramesNotBytes()
{
    // Mono, so frames and samples coincide and the order is unambiguous.
    QByteArray pcm(8, Qt::Uninitialized);
    auto* samples = reinterpret_cast<int16_t*>(pcm.data());
    samples[0] = 100; samples[1] = 200; samples[2] = 300; samples[3] = 400;

    const QByteArray reversed = atk::audio::ScrubAudioEngine::reverseFrames(pcm, 1);
    const auto* out = reinterpret_cast<const int16_t*>(reversed.constData());

    // Sample values survive intact and only their order changes. Reversing at
    // byte level would corrupt every sample into noise.
    QCOMPARE(reversed.size(), pcm.size());
    QCOMPARE(out[0], int16_t(400));
    QCOMPARE(out[1], int16_t(300));
    QCOMPARE(out[2], int16_t(200));
    QCOMPARE(out[3], int16_t(100));
}

void TestScrubAudio::reversalPreservesChannelOrder()
{
    // Stereo: frames are (L,R) pairs. Frame order reverses; L and R must not
    // swap, or backward scrubbing would flip the stereo image.
    QByteArray pcm(12, Qt::Uninitialized);
    auto* samples = reinterpret_cast<int16_t*>(pcm.data());
    samples[0] = 1; samples[1] = -1;   // frame 0: L=1,  R=-1
    samples[2] = 2; samples[3] = -2;   // frame 1
    samples[4] = 3; samples[5] = -3;   // frame 2

    const QByteArray reversed = atk::audio::ScrubAudioEngine::reverseFrames(pcm, 2);
    const auto* out = reinterpret_cast<const int16_t*>(reversed.constData());

    QCOMPARE(out[0], int16_t(3));  QCOMPARE(out[1], int16_t(-3));
    QCOMPARE(out[2], int16_t(2));  QCOMPARE(out[3], int16_t(-2));
    QCOMPARE(out[4], int16_t(1));  QCOMPARE(out[5], int16_t(-1));
}

void TestScrubAudio::reversalIsItsOwnInverse()
{
    QByteArray pcm(64, Qt::Uninitialized);
    auto* samples = reinterpret_cast<int16_t*>(pcm.data());
    for (int i = 0; i < 32; ++i) {
        samples[i] = static_cast<int16_t>(i * 37 - 500);
    }

    const QByteArray once = atk::audio::ScrubAudioEngine::reverseFrames(pcm, 2);
    const QByteArray twice = atk::audio::ScrubAudioEngine::reverseFrames(once, 2);
    QCOMPARE(twice, pcm);
}

void TestScrubAudio::refusesToReverseATrivialBuffer()
{
    // Too short to have a meaningful order; returned unchanged rather than
    // reinterpreted through a bad frame count.
    const QByteArray tiny(2, char(0));
    QCOMPARE(atk::audio::ScrubAudioEngine::reverseFrames(tiny, 2), tiny);
    QCOMPARE(atk::audio::ScrubAudioEngine::reverseFrames(QByteArray(), 2), QByteArray());
}

QTEST_GUILESS_MAIN(TestScrubAudio)
#include "tst_scrubaudio.moc"
