#include "media/MediaDecoder.h"

#include "media/FrameCache.h"
#include "media/MediaMetadata.h"
#include "media/VideoFrame.h"

#include <QDir>
#include <QFileInfo>
#include <QTest>

using atk::media::AudioChunk;
using atk::media::AudioFormat;
using atk::media::DecodeStatus;
using atk::media::FrameCache;
using atk::media::FrameCountSource;
using atk::media::MediaDecoder;
using atk::media::VideoFrame;

namespace {

/// The fixture is generated at build time by cmake/ATKTestMedia.cmake and
/// validated by the `fixture_validation` test before these run.
///
///   640x360, exactly 24 fps, exactly 2.0 s  ->  48 frames, indices 0..47
///   FFV1 video (lossless, all-intra) + PCM s16le audio at 48 kHz, in Matroska
constexpr int64_t kExpectedFrameCount = 48;
constexpr int kExpectedWidth = 640;
constexpr int kExpectedHeight = 360;
constexpr int kExpectedFps = 24;
constexpr int kExpectedSampleRate = 48000;

QString fixtureDir()
{
    return QString::fromUtf8(ATK_TEST_MEDIA_DIR);
}

QString losslessFixture()
{
    return QDir(fixtureDir()).filePath(QStringLiteral("atk_fixture_48f.mkv"));
}

/// The lossy MP4 exercises a different container and a codec whose seek
/// behaviour is not all-intra.
QString lossyFixture()
{
    return QDir(fixtureDir()).filePath(QStringLiteral("atk_fixture_48f.mp4"));
}

} // namespace

/// Exercises the real FFmpeg decoder against generated fixtures.
///
/// These are the tests that justify the phrase "frame accurate". Everything
/// here goes through actual decoding: nothing asserts on an integer counter
/// that the player maintains for itself.
class TestMediaDecoder : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- Opening ----------------------------------------------------------
    void opensFixture();
    void readsMetadata();
    void detectsBothStreams();
    void reportsExactFrameCount();
    void rejectsMissingFile();
    void rejectsNonMediaFile();
    void repeatedOpenCloseIsClean();

    // --- Decoding ---------------------------------------------------------
    void decodesFirstFrame();
    void decodesSequentialFramesInPresentationOrder();
    void decodesEveryExpectedFrame();
    void reportsEndOfFile();

    // --- Frame accuracy ---------------------------------------------------
    void stepsForwardOneRealFrame();
    void stepsBackwardOneRealFrame();
    void steppingSequenceMatchesDecodedFrames();
    void randomAccessReturnsRequestedFrame_data();
    void randomAccessReturnsRequestedFrame();
    void nonKeyframeSeekLandsOnRequestedFrame();
    void seekToFinalFrameIsReachable();
    void repeatedSeekToSameFrameIsStable();

    // --- Cancellation -----------------------------------------------------
    void cancellationAbandonsDecode();

    // --- Audio ------------------------------------------------------------
    void decodesAudioSamples();
    void resamplesToRequestedFormat();
    void resamplesToMonoAndDifferentRate();

    // --- Cache ------------------------------------------------------------
    void cacheRejectsForeignSourceGeneration();
};

void TestMediaDecoder::initTestCase()
{
    if (!QFileInfo::exists(losslessFixture())) {
        QSKIP("Generated test media not found; build the atk_test_media target first.");
    }
}

// ---------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------

void TestMediaDecoder::opensFixture()
{
    MediaDecoder decoder;
    QString error;
    QVERIFY2(decoder.open(losslessFixture(), &error), qPrintable(error));
    QVERIFY(decoder.isOpen());
}

void TestMediaDecoder::readsMetadata()
{
    MediaDecoder decoder;
    QString error;
    QVERIFY2(decoder.open(losslessFixture(), &error), qPrintable(error));

    const auto& meta = decoder.metadata();

    QCOMPARE(meta.resolution.width(), kExpectedWidth);
    QCOMPARE(meta.resolution.height(), kExpectedHeight);
    QCOMPARE(meta.fileName, QStringLiteral("atk_fixture_48f.mkv"));

    // The rate must survive as an exact rational, not a rounded double.
    QCOMPARE(meta.frameRate.numerator, kExpectedFps);
    QCOMPARE(meta.frameRate.denominator, 1);
    QCOMPARE(meta.frameRate.toDouble(), 24.0);

    QVERIFY(meta.videoTimeBase.isValid());
    QCOMPARE(meta.videoCodecName, QStringLiteral("ffv1"));

    // 2 seconds, within a frame's worth of tolerance.
    QVERIFY(meta.durationUs > 1'900'000);
    QVERIFY(meta.durationUs < 2'100'000);
    QVERIFY(qAbs(meta.durationSeconds() - 2.0) < 0.05);

    QVERIFY(!meta.pixelFormatName.isEmpty());
    QVERIFY(meta.isValid());
}

void TestMediaDecoder::detectsBothStreams()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    const auto& meta = decoder.metadata();

    QVERIFY(meta.hasVideo);
    QVERIFY(meta.videoStreamIndex >= 0);

    QVERIFY(meta.hasAudio);
    QVERIFY(meta.audioStreamIndex >= 0);
    QVERIFY(meta.videoStreamIndex != meta.audioStreamIndex);

    QCOMPARE(meta.audioSampleRate, kExpectedSampleRate);
    QVERIFY(meta.audioChannelCount >= 1);
    QVERIFY(!meta.audioChannelLayout.isEmpty());
    QCOMPARE(meta.audioCodecName, QStringLiteral("pcm_s16le"));
}

void TestMediaDecoder::reportsExactFrameCount()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    QString error;
    const int64_t counted = decoder.countFramesExactly(&error);
    QCOMPARE(counted, kExpectedFrameCount);
}

void TestMediaDecoder::rejectsMissingFile()
{
    MediaDecoder decoder;
    QString error;

    QVERIFY(!decoder.open(QStringLiteral("C:/definitely/not/here_9f3a.mkv"), &error));
    QVERIFY(!decoder.isOpen());

    // The message has to be usable in the UI, not an FFmpeg error number.
    QVERIFY(!error.isEmpty());
    bool numeric = false;
    error.toLongLong(&numeric);
    QVERIFY(!numeric);
}

void TestMediaDecoder::rejectsNonMediaFile()
{
    // A text file is a container FFmpeg cannot demux; it must fail, not crash.
    const QString path = QDir(fixtureDir()).filePath(QStringLiteral("not_media.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not a video file");
    file.close();

    MediaDecoder decoder;
    QString error;
    QVERIFY(!decoder.open(path, &error));
    QVERIFY(!decoder.isOpen());
    QVERIFY(!error.isEmpty());

    QFile::remove(path);
}

void TestMediaDecoder::repeatedOpenCloseIsClean()
{
    // Every FFmpeg object is RAII-owned, so repeated cycles must not accumulate
    // anything. This is the cheap regression guard for a leak in the open path.
    MediaDecoder decoder;
    for (int i = 0; i < 12; ++i) {
        QString error;
        QVERIFY2(decoder.open(losslessFixture(), &error), qPrintable(error));

        VideoFrame frame;
        QCOMPARE(decoder.nextVideoFrame(frame, nullptr), DecodeStatus::Ok);
        QVERIFY(frame.isValid());

        decoder.close();
        QVERIFY(!decoder.isOpen());
    }

    // Reopening after the loop still works.
    QVERIFY(decoder.open(losslessFixture(), nullptr));
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

void TestMediaDecoder::decodesFirstFrame()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    VideoFrame frame;
    QString error;
    QCOMPARE(decoder.nextVideoFrame(frame, &error), DecodeStatus::Ok);

    QVERIFY(frame.isValid());
    QCOMPARE(frame.frameIndex, qint64(0));
    QCOMPARE(frame.width, kExpectedWidth);
    QCOMPARE(frame.height, kExpectedHeight);
    QCOMPARE(frame.image.width(), kExpectedWidth);
    QCOMPARE(frame.image.height(), kExpectedHeight);
    QVERIFY(!frame.image.isNull());
    QCOMPARE(frame.image.format(), QImage::Format_RGB32);
    QCOMPARE(frame.ptsUs, qint64(0));
}

void TestMediaDecoder::decodesSequentialFramesInPresentationOrder()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    int64_t previousPts = -1;
    for (int64_t expected = 0; expected < 10; ++expected) {
        VideoFrame frame;
        QString error;
        QCOMPARE(decoder.nextVideoFrame(frame, &error), DecodeStatus::Ok);

        // Indices come out consecutively and timestamps strictly increase --
        // that is what "presentation order" means here, as opposed to the
        // order packets happened to arrive in.
        QCOMPARE(frame.frameIndex, expected);
        QVERIFY(frame.ptsUs > previousPts);
        previousPts = frame.ptsUs;
    }
}

void TestMediaDecoder::decodesEveryExpectedFrame()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    int64_t decoded = 0;
    int64_t lastIndex = -1;

    while (true) {
        VideoFrame frame;
        QString error;
        const DecodeStatus status = decoder.nextVideoFrame(frame, &error);
        if (status == DecodeStatus::EndOfFile) {
            break;
        }
        QVERIFY2(status == DecodeStatus::Ok, qPrintable(error));
        QVERIFY(frame.isValid());
        lastIndex = frame.frameIndex;
        ++decoded;
    }

    QCOMPARE(decoded, kExpectedFrameCount);
    QCOMPARE(lastIndex, kExpectedFrameCount - 1);
}

void TestMediaDecoder::reportsEndOfFile()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    VideoFrame frame;
    while (decoder.nextVideoFrame(frame, nullptr) == DecodeStatus::Ok) {
        // drain
    }

    // Asking again past the end keeps reporting EOF rather than erroring.
    QCOMPARE(decoder.nextVideoFrame(frame, nullptr), DecodeStatus::EndOfFile);
    QCOMPARE(decoder.nextVideoFrame(frame, nullptr), DecodeStatus::EndOfFile);
}

// ---------------------------------------------------------------------------
// Frame accuracy
// ---------------------------------------------------------------------------

void TestMediaDecoder::stepsForwardOneRealFrame()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    VideoFrame frame;
    QString error;
    QVERIFY2(decoder.frameAtIndex(10, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, qint64(10));

    QVERIFY2(decoder.frameAtIndex(11, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, qint64(11));
}

void TestMediaDecoder::stepsBackwardOneRealFrame()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    VideoFrame frame;
    QString error;
    QVERIFY2(decoder.frameAtIndex(20, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, qint64(20));

    // Backward stepping is the hard direction: it requires seeking to an
    // earlier keyframe and decoding forward again. The frame returned must be
    // 19, never the keyframe the seek happened to land on.
    QVERIFY2(decoder.frameAtIndex(19, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, qint64(19));
}

void TestMediaDecoder::steppingSequenceMatchesDecodedFrames()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(lossyFixture(), nullptr));

    // The exact sequence the milestone calls for: 0 -> 1 -> 2 -> 1, verified
    // against the decoder rather than against a counter the player keeps.
    const QList<qint64> sequence = { 0, 1, 2, 1, 2, 3, 2, 1, 0 };

    for (const qint64 requested : sequence) {
        VideoFrame frame;
        QString error;
        QVERIFY2(decoder.frameAtIndex(requested, frame, &error),
                 qPrintable(QStringLiteral("frame %1: %2").arg(requested).arg(error)));
        QCOMPARE(frame.frameIndex, requested);
        QVERIFY(!frame.image.isNull());
    }
}

void TestMediaDecoder::randomAccessReturnsRequestedFrame_data()
{
    QTest::addColumn<qint64>("frameIndex");

    QTest::newRow("first")       << qint64(0);
    QTest::newRow("second")      << qint64(1);
    QTest::newRow("early")       << qint64(5);
    QTest::newRow("middle")      << qint64(24);
    QTest::newRow("late")        << qint64(40);
    QTest::newRow("penultimate") << qint64(46);
    QTest::newRow("final")       << qint64(47);
}

void TestMediaDecoder::randomAccessReturnsRequestedFrame()
{
    QFETCH(qint64, frameIndex);

    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    VideoFrame frame;
    QString error;
    QVERIFY2(decoder.frameAtIndex(frameIndex, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, frameIndex);
    QVERIFY(!frame.image.isNull());
}

void TestMediaDecoder::nonKeyframeSeekLandsOnRequestedFrame()
{
    // MPEG-4 has real inter-frame dependencies, so most of these targets are
    // not keyframes. Seeking lands on an earlier keyframe and the decoder has
    // to run forward from there; returning the keyframe instead would be the
    // classic wrong answer.
    MediaDecoder decoder;
    QVERIFY(decoder.open(lossyFixture(), nullptr));

    for (const qint64 target : { qint64(7), qint64(13), qint64(29), qint64(31), qint64(45) }) {
        VideoFrame frame;
        QString error;
        QVERIFY2(decoder.frameAtIndex(target, frame, &error),
                 qPrintable(QStringLiteral("frame %1: %2").arg(target).arg(error)));
        QCOMPARE(frame.frameIndex, target);
    }
}

void TestMediaDecoder::seekToFinalFrameIsReachable()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    VideoFrame frame;
    QString error;
    const qint64 last = kExpectedFrameCount - 1;

    QVERIFY2(decoder.frameAtIndex(last, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, last);

    // Stepping back from the last frame still works after that seek.
    QVERIFY2(decoder.frameAtIndex(last - 1, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, last - 1);
}

void TestMediaDecoder::repeatedSeekToSameFrameIsStable()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    // Asking for the same frame repeatedly must keep giving the same answer;
    // a decoder that drifts on repeated seeks fails here.
    for (int i = 0; i < 6; ++i) {
        VideoFrame frame;
        QString error;
        QVERIFY2(decoder.frameAtIndex(33, frame, &error), qPrintable(error));
        QCOMPARE(frame.frameIndex, qint64(33));
    }
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

void TestMediaDecoder::cancellationAbandonsDecode()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(lossyFixture(), nullptr));

    VideoFrame frame;
    QString error;

    // A predicate that is already true stands in for "a newer seek arrived".
    // The decode must give up and report failure rather than returning a frame
    // the caller would then have to recognise as stale.
    const bool decoded = decoder.frameAtIndex(45, frame, &error, [] { return true; });
    QVERIFY(!decoded);

    // The decoder stays usable afterwards.
    QVERIFY2(decoder.frameAtIndex(5, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, qint64(5));
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void TestMediaDecoder::decodesAudioSamples()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));
    QVERIFY(decoder.metadata().hasAudio);

    AudioFormat format;
    format.sampleRate = kExpectedSampleRate;
    format.channelCount = 2;
    format.bytesPerSample = 2;

    QString error;
    QVERIFY2(decoder.configureAudioOutput(format, &error), qPrintable(error));

    int64_t totalBytes = 0;
    int chunks = 0;
    while (chunks < 40) {
        AudioChunk chunk;
        if (decoder.nextAudioChunk(chunk, &error) != DecodeStatus::Ok) {
            break;
        }
        QVERIFY(chunk.isValid());
        QVERIFY(chunk.ptsUs >= 0);
        totalBytes += chunk.pcm.size();
        ++chunks;
    }

    QVERIFY(chunks > 0);
    QVERIFY(totalBytes > 0);

    // The fixture is a 440 Hz tone, so the decoded PCM cannot be silence.
    QVERIFY(totalBytes >= format.bytesPerFrame());
}

void TestMediaDecoder::resamplesToRequestedFormat()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    AudioFormat format;
    format.sampleRate = kExpectedSampleRate;
    format.channelCount = 2;
    format.bytesPerSample = 2;
    QVERIFY(decoder.configureAudioOutput(format, nullptr));
    QCOMPARE(decoder.outputAudioFormat(), format);

    AudioChunk chunk;
    QString error;
    QCOMPARE(decoder.nextAudioChunk(chunk, &error), DecodeStatus::Ok);

    // Interleaved stereo 16-bit: every chunk is a whole number of sample frames.
    QCOMPARE(chunk.pcm.size() % format.bytesPerFrame(), qsizetype(0));

    // Not all zeroes -- a resampler that silently produced silence would pass a
    // size check but fail here.
    bool anyNonZero = false;
    for (const char byte : chunk.pcm) {
        if (byte != 0) {
            anyNonZero = true;
            break;
        }
    }
    QVERIFY(anyNonZero);
}

void TestMediaDecoder::resamplesToMonoAndDifferentRate()
{
    // A device that only offers 44.1 kHz mono must still work; the resampler is
    // what absorbs the difference rather than the caller assuming 48 kHz stereo.
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    AudioFormat format;
    format.sampleRate = 44100;
    format.channelCount = 1;
    format.bytesPerSample = 2;

    QString error;
    QVERIFY2(decoder.configureAudioOutput(format, &error), qPrintable(error));
    QCOMPARE(decoder.outputAudioFormat().sampleRate, 44100);
    QCOMPARE(decoder.outputAudioFormat().channelCount, 1);

    AudioChunk chunk;
    QCOMPARE(decoder.nextAudioChunk(chunk, &error), DecodeStatus::Ok);
    QVERIFY(chunk.isValid());
    QCOMPARE(chunk.pcm.size() % format.bytesPerFrame(), qsizetype(0));
}

// ---------------------------------------------------------------------------
// Cache identity
// ---------------------------------------------------------------------------

void TestMediaDecoder::cacheRejectsForeignSourceGeneration()
{
    MediaDecoder decoder;
    QVERIFY(decoder.open(losslessFixture(), nullptr));

    FrameCache cache;
    cache.setSourceGeneration(7);

    VideoFrame frame;
    QVERIFY(decoder.nextVideoFrame(frame, nullptr) == DecodeStatus::Ok);

    // A frame tagged with a different source must not enter the cache, or it
    // could later be displayed under a file it does not belong to.
    frame.sourceGeneration = 6;
    cache.insert(frame);
    QCOMPARE(cache.count(), std::size_t(0));

    frame.sourceGeneration = 7;
    cache.insert(frame);
    QCOMPARE(cache.count(), std::size_t(1));
    QVERIFY(cache.find(frame.frameIndex) != nullptr);

    // Changing generation discards everything held for the previous source.
    cache.setSourceGeneration(8);
    QCOMPARE(cache.count(), std::size_t(0));
    QVERIFY(cache.find(frame.frameIndex) == nullptr);
}

QTEST_GUILESS_MAIN(TestMediaDecoder)
#include "tst_mediadecoder.moc"
