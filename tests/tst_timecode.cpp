#include "timeline/Timecode.h"

#include <QTest>

using atk::media::FrameRate;
namespace tc = atk::timeline::timecode;

class TestTimecode : public QObject {
    Q_OBJECT

private slots:
    void formatsWholeRates_data();
    void formatsWholeRates();

    void usesNominalRateForBroadcastRates();
    void returnsPlaceholderForUnknownRate();
    void handlesNegativeFrames();

    void roundTripsThroughParse_data();
    void roundTripsThroughParse();

    void rejectsMalformedInput_data();
    void rejectsMalformedInput();
};

void TestTimecode::formatsWholeRates_data()
{
    QTest::addColumn<qint64>("frame");
    QTest::addColumn<int>("fps");
    QTest::addColumn<QString>("expected");

    QTest::newRow("zero")            << qint64(0)     << 24 << "00:00:00:00";
    QTest::newRow("last of second")  << qint64(23)    << 24 << "00:00:00:23";
    QTest::newRow("one second")      << qint64(24)    << 24 << "00:00:01:00";
    QTest::newRow("one minute")      << qint64(1440)  << 24 << "00:01:00:00";
    QTest::newRow("one hour at 25")  << qint64(90000) << 25 << "01:00:00:00";
}

void TestTimecode::formatsWholeRates()
{
    QFETCH(qint64, frame);
    QFETCH(int, fps);
    QFETCH(QString, expected);

    QCOMPARE(tc::fromFrame(frame, FrameRate::fromInteger(fps)), expected);
}

void TestTimecode::usesNominalRateForBroadcastRates()
{
    // 23.976 counts 24 frames per timecode second, not 23.
    const FrameRate ntscFilm{ 24000, 1001 };
    QCOMPARE(tc::fromFrame(23, ntscFilm), QStringLiteral("00:00:00:23"));
    QCOMPARE(tc::fromFrame(24, ntscFilm), QStringLiteral("00:00:01:00"));

    const FrameRate ntscVideo{ 30000, 1001 };
    QCOMPARE(tc::fromFrame(30, ntscVideo), QStringLiteral("00:00:01:00"));
}

void TestTimecode::returnsPlaceholderForUnknownRate()
{
    QCOMPARE(tc::fromFrame(100, FrameRate{}), tc::placeholder());
    QCOMPARE(tc::toFrame(QStringLiteral("00:00:01:00"), FrameRate{}), qint64(-1));
}

void TestTimecode::handlesNegativeFrames()
{
    // Negative frames occur when an A/B source carries a negative offset.
    QCOMPARE(tc::fromFrame(-1, FrameRate::fromInteger(24)), QStringLiteral("-00:00:00:01"));
}

void TestTimecode::roundTripsThroughParse_data()
{
    QTest::addColumn<qint64>("frame");
    QTest::addColumn<int>("fps");

    QTest::newRow("zero")     << qint64(0)      << 24;
    QTest::newRow("small")    << qint64(37)     << 24;
    QTest::newRow("minutes")  << qint64(5000)   << 25;
    QTest::newRow("hours")    << qint64(400000) << 30;
}

void TestTimecode::roundTripsThroughParse()
{
    QFETCH(qint64, frame);
    QFETCH(int, fps);

    const FrameRate rate = FrameRate::fromInteger(fps);
    QCOMPARE(tc::toFrame(tc::fromFrame(frame, rate), rate), frame);
}

void TestTimecode::rejectsMalformedInput_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty")            << QString();
    QTest::newRow("words")            << QStringLiteral("not a timecode");
    QTest::newRow("too few fields")   << QStringLiteral("00:00:01");
    QTest::newRow("minutes overflow") << QStringLiteral("00:99:00:00");
    QTest::newRow("frames overflow")  << QStringLiteral("00:00:00:24");
}

void TestTimecode::rejectsMalformedInput()
{
    QFETCH(QString, text);
    QCOMPARE(tc::toFrame(text, FrameRate::fromInteger(24)), qint64(-1));
}

QTEST_GUILESS_MAIN(TestTimecode)
#include "tst_timecode.moc"
