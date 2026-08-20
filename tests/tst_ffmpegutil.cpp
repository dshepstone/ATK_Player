#include "media/ffmpeg/FFmpegUtil.h"

#include <QTest>

extern "C" {
#include <libavutil/error.h>
}

namespace ff = atk::media::ffmpeg;

/// Covers error translation and the rational time arithmetic every timestamp in
/// the player passes through.
class TestFFmpegUtil : public QObject {
    Q_OBJECT

private slots:
    void translatesKnownErrorCodes();
    void errorStringNeverReturnsBareNumber();
    void errorStringIncludesContextAndCode();
    void reportsLibraryVersions();

    void validatesRationals();

    void convertsPtsToMicroseconds();
    void convertsMicrosecondsToPts();

    void frameIndexToPtsRoundTrips_data();
    void frameIndexToPtsRoundTrips();

    void honoursStreamStartTime();
    void ptsToFrameIndexRoundsToNearest();
    void handlesBroadcastRatesExactly();
    void handlesInvalidInputSafely();
};

void TestFFmpegUtil::translatesKnownErrorCodes()
{
    // AVERROR_EOF is the code the decode loop checks most often, so its
    // message must be readable rather than a packed integer.
    const QString message = ff::errorString(AVERROR_EOF);
    QVERIFY(!message.isEmpty());
    QVERIFY(!message.contains(QStringLiteral("unknown FFmpeg error")));
}

void TestFFmpegUtil::errorStringNeverReturnsBareNumber()
{
    // The point of the helper: an operator should never be shown -1094995529.
    const QString message = ff::errorString(AVERROR(ENOENT));
    QVERIFY(!message.isEmpty());
    bool numeric = false;
    message.toLongLong(&numeric);
    QVERIFY(!numeric);
}

void TestFFmpegUtil::errorStringIncludesContextAndCode()
{
    const QString message = ff::errorString("avformat_open_input", AVERROR(ENOENT));
    QVERIFY(message.startsWith(QStringLiteral("avformat_open_input: ")));
    QVERIFY(message.contains(QString::number(AVERROR(ENOENT))));
}

void TestFFmpegUtil::reportsLibraryVersions()
{
    const QString versions = ff::libraryVersions();
    QVERIFY(versions.contains(QStringLiteral("avformat")));
    QVERIFY(versions.contains(QStringLiteral("avcodec")));
    QVERIFY(versions.contains(QStringLiteral("swscale")));
    QVERIFY(versions.contains(QStringLiteral("swresample")));
}

void TestFFmpegUtil::validatesRationals()
{
    QVERIFY(ff::isValidRational(AVRational{ 24, 1 }));
    QVERIFY(ff::isValidRational(AVRational{ 24000, 1001 }));
    QVERIFY(!ff::isValidRational(AVRational{ 0, 1 }));
    QVERIFY(!ff::isValidRational(AVRational{ 24, 0 }));
    QVERIFY(!ff::isValidRational(AVRational{ -24, 1 }));
}

void TestFFmpegUtil::convertsPtsToMicroseconds()
{
    // A 1/1000 time base means the pts is already milliseconds.
    QCOMPARE(ff::ptsToMicroseconds(1000, AVRational{ 1, 1000 }), qint64(1'000'000));
    QCOMPARE(ff::ptsToMicroseconds(0, AVRational{ 1, 1000 }), qint64(0));

    // A 1/90000 time base is the MPEG norm.
    QCOMPARE(ff::ptsToMicroseconds(90000, AVRational{ 1, 90000 }), qint64(1'000'000));
}

void TestFFmpegUtil::convertsMicrosecondsToPts()
{
    QCOMPARE(ff::microsecondsToPts(1'000'000, AVRational{ 1, 1000 }), qint64(1000));
    QCOMPARE(ff::microsecondsToPts(1'000'000, AVRational{ 1, 90000 }), qint64(90000));
}

void TestFFmpegUtil::frameIndexToPtsRoundTrips_data()
{
    QTest::addColumn<qint64>("frameIndex");
    QTest::addColumn<int>("rateNum");
    QTest::addColumn<int>("rateDen");
    QTest::addColumn<int>("timeBaseDen");

    QTest::newRow("24fps frame 0")      << qint64(0)    << 24    << 1    << 12288;
    QTest::newRow("24fps frame 1")      << qint64(1)    << 24    << 1    << 12288;
    QTest::newRow("24fps frame 47")     << qint64(47)   << 24    << 1    << 12288;
    QTest::newRow("24fps frame 10000")  << qint64(10000)<< 24    << 1    << 12288;
    QTest::newRow("25fps frame 500")    << qint64(500)  << 25    << 1    << 90000;
    QTest::newRow("23.976 frame 1000")  << qint64(1000) << 24000 << 1001 << 90000;
    QTest::newRow("29.97 frame 1799")   << qint64(1799) << 30000 << 1001 << 90000;
}

void TestFFmpegUtil::frameIndexToPtsRoundTrips()
{
    QFETCH(qint64, frameIndex);
    QFETCH(int, rateNum);
    QFETCH(int, rateDen);
    QFETCH(int, timeBaseDen);

    const AVRational rate{ rateNum, rateDen };
    const AVRational timeBase{ 1, timeBaseDen };

    const qint64 pts = ff::frameIndexToPts(frameIndex, rate, timeBase, 0);
    const qint64 back = ff::ptsToFrameIndex(pts, rate, timeBase, 0);

    // This round trip is what frame stepping depends on: an index converted to
    // a timestamp and back must land on the same frame, or stepping would drift.
    QCOMPARE(back, frameIndex);
}

void TestFFmpegUtil::honoursStreamStartTime()
{
    const AVRational rate{ 24, 1 };
    const AVRational timeBase{ 1, 1000 };
    constexpr qint64 startTime = 5000; // stream begins 5 s in

    const qint64 pts = ff::frameIndexToPts(10, rate, timeBase, startTime);
    QVERIFY(pts > startTime);

    // Ignoring start_time would put every index off by a constant.
    QCOMPARE(ff::ptsToFrameIndex(pts, rate, timeBase, startTime), qint64(10));
    QCOMPARE(ff::ptsToFrameIndex(startTime, rate, timeBase, startTime), qint64(0));
}

void TestFFmpegUtil::ptsToFrameIndexRoundsToNearest()
{
    const AVRational rate{ 24, 1 };
    const AVRational timeBase{ 1, 1000 };

    // Frame 1 at 24 fps sits at 41.666 ms. A container that stores 41 or 42
    // must still resolve to frame 1, not frame 0.
    QCOMPARE(ff::ptsToFrameIndex(41, rate, timeBase, 0), qint64(1));
    QCOMPARE(ff::ptsToFrameIndex(42, rate, timeBase, 0), qint64(1));
}

void TestFFmpegUtil::handlesBroadcastRatesExactly()
{
    const AVRational ntscFilm{ 24000, 1001 };

    // One second of 23.976 is 24000/1001 frames; frame 24 lands just past 1 s.
    const qint64 usAtFrame24 = ff::frameIndexToMicroseconds(24, ntscFilm);
    QVERIFY(usAtFrame24 > 1'000'000);
    QVERIFY(usAtFrame24 < 1'002'000);

    // Exactness over a long run is the whole reason the rate stays rational:
    // a double would have drifted by now.
    for (const qint64 index : { qint64(0), qint64(1), qint64(24), qint64(100000) }) {
        const qint64 us = ff::frameIndexToMicroseconds(index, ntscFilm);
        QCOMPARE(ff::microsecondsToFrameIndex(us, ntscFilm), index);
    }
}

void TestFFmpegUtil::handlesInvalidInputSafely()
{
    const AVRational invalid{ 0, 0 };

    QCOMPARE(ff::ptsToMicroseconds(1000, invalid), qint64(0));
    QCOMPARE(ff::microsecondsToPts(1000, invalid), qint64(0));
    QCOMPARE(ff::frameIndexToMicroseconds(10, invalid), qint64(0));
    QCOMPARE(ff::microsecondsToFrameIndex(10, invalid), qint64(0));

    // AV_NOPTS_VALUE must not be treated as a real timestamp.
    QCOMPARE(ff::ptsToMicroseconds(AV_NOPTS_VALUE, AVRational{ 1, 1000 }), qint64(0));
    QCOMPARE(ff::ptsToFrameIndex(AV_NOPTS_VALUE, AVRational{ 24, 1 },
                                 AVRational{ 1, 1000 }, 0),
             qint64(0));

    // Negative relative positions clamp to the first frame.
    QCOMPARE(ff::ptsToFrameIndex(-500, AVRational{ 24, 1 }, AVRational{ 1, 1000 }, 0),
             qint64(0));
}

QTEST_GUILESS_MAIN(TestFFmpegUtil)
#include "tst_ffmpegutil.moc"
