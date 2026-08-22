#include "playback/PlaybackController.h"
#include "media/ffmpeg/FFmpegUtil.h"

#include "timeline/TimelineModel.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>

using atk::playback::PlaybackController;
using atk::playback::PlayerState;
using atk::timeline::TimelineModel;

namespace {

QString syncFixture()
{
    return QDir(QString::fromUtf8(ATK_TEST_MEDIA_DIR))
        .filePath(QStringLiteral("atk_sync_10s.mkv"));
}

QString longFixture()
{
    return qEnvironmentVariable("ATK_LONG_TEST_MEDIA");
}

QString manualMediaDirectory()
{
    return qEnvironmentVariable("ATK_MANUAL_MEDIA_DIR");
}

QString rangeStartFixture()
{
    const QString overridePath = qEnvironmentVariable("ATK_RANGE_TEST_MEDIA");
    return overridePath.isEmpty() ? syncFixture() : overridePath;
}

double percentile(QVector<double> values, double fraction)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const qsizetype index = std::clamp<qsizetype>(
        static_cast<qsizetype>(std::ceil(fraction * values.size())) - 1,
        0, values.size() - 1);
    return values.at(index);
}

struct Fixture {
    TimelineModel timeline;
    PlaybackController playback{ &timeline };

    bool open(const QString& path = syncFixture())
    {
        QSignalSpy opened(&playback, &PlaybackController::mediaOpened);
        playback.openMedia(path);
        return opened.wait(10000) && playback.state() == PlayerState::Ready;
    }
};

} // namespace

class TestPlaybackInteraction : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void rapidForwardPreservesInputsAndProgress();
    void rapidForwardAndBackwardAccumulate();
    void rapidRoundTripPreservesInputsAndProgress();
    void scrubCoalescesToExactReleaseFrame();
    void longClipScrubProfile();
    void manualMediaScrubProfile_data();
    void manualMediaScrubProfile();
    void explicitSeekResetsLogicalTarget();
    void realTimePlaybackCrossesLoopBoundary();
    void frameStepAudioUsesExactTargetAndDirection();
    void automaticRangeStartMatchesManualSeekEpoch();
    void stoppedRangeMutationReanchorsAtCurrentFrame();
    void ordinaryPauseResumeKeepsEpochClean();
    void pauseAfterPlaybackAnchorsFrameStep();
    void playingDirectStepAnchorsToPresentedFrame();
    void endedBackwardAnchorsToFinalFrame();
    void shortRangeLoopsKeepSynchronizedEpoch();
    void playingRangeBookmarkActivationRestartsExactly();
};

void TestPlaybackInteraction::pauseAfterPlaybackAnchorsFrameStep()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.setMuted(true);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.timeline.currentFrame() >= 30, 5000);
    fixture.playback.pause();
    const qint64 paused = fixture.timeline.currentFrame();
    QVERIFY(paused >= 30);

    fixture.playback.stepForward();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.timeline.currentFrame(), paused + 1, 5000);
    fixture.playback.stepBackward();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.timeline.currentFrame(), paused, 5000);
}

void TestPlaybackInteraction::playingDirectStepAnchorsToPresentedFrame()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.setMuted(true);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.timeline.currentFrame() >= 20, 5000);
    const qint64 presented = fixture.timeline.currentFrame();

    fixture.playback.stepForward();
    QVERIFY(fixture.playback.state() != PlayerState::Playing);
    QCOMPARE(fixture.playback.navigationFrame(), presented + 1);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.timeline.currentFrame(), presented + 1, 5000);
}

void TestPlaybackInteraction::endedBackwardAnchorsToFinalFrame()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.setMuted(true);
    fixture.playback.play();
    fixture.playback.goToEnd();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Ended, 5000);
    const qint64 last = fixture.timeline.effectiveEndFrame();
    QCOMPARE(fixture.timeline.currentFrame(), last);

    fixture.playback.stepBackward();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.timeline.currentFrame(), last - 1, 5000);
}

void TestPlaybackInteraction::playingRangeBookmarkActivationRestartsExactly()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.setLoopEnabled(true);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.timeline.currentFrame() >= 3, 5000);

    QSignalSpy frames(&fixture.playback, &PlaybackController::frameChanged);
    fixture.playback.activateReviewRange(20, 30);
    QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 5000);
    QCOMPARE(qvariant_cast<atk::media::VideoFrame>(frames.first().at(0)).frameIndex, qint64(20));
    QCOMPARE(fixture.timeline.viewport().startFrame(), qint64(20));
    QCOMPARE(fixture.timeline.viewport().endFrame(), qint64(30));
    QVERIFY(fixture.playback.isLoopEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Playing, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.lastAudioEpochUs(),
                              fixture.playback.playbackOriginUs(), 5000);
    const auto rate = fixture.playback.metadata().frameRate;
    const qint64 frameUs = atk::media::ffmpeg::frameIndexToMicroseconds(
        1, AVRational{rate.numerator, rate.denominator});
    QVERIFY(qAbs(fixture.playback.lastAudioEpochUs() - fixture.playback.playbackOriginUs())
            < frameUs);
    fixture.playback.pause();
}

void TestPlaybackInteraction::frameStepAudioUsesExactTargetAndDirection()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);
    QSignalSpy requests(&fixture.playback, &PlaybackController::reviewAudioRequested);

    fixture.playback.stepForward();
    QCOMPARE(requests.count(), 0); // independently off by default

    fixture.playback.setFrameStepAudioEnabled(true);
    fixture.playback.stepForward();
    QCOMPARE(requests.count(), 1);
    const auto rate = fixture.playback.metadata().frameRate;
    const qint64 expectedUs = atk::media::ffmpeg::frameIndexToMicroseconds(
        2, AVRational{rate.numerator, rate.denominator});
    QCOMPARE(requests.at(0).at(0).toLongLong(), expectedUs);
    QCOMPARE(requests.at(0).at(1).toBool(), false);

    fixture.playback.stepBackward();
    QCOMPARE(requests.count(), 2);
    QCOMPARE(requests.at(1).at(1).toBool(), true);

    fixture.playback.setMuted(true);
    fixture.playback.stepForward();
    QCOMPARE(requests.count(), 2);

    fixture.playback.setMuted(false);
    for (int i = 0; i < 10; ++i) fixture.playback.stepForward();
    QCOMPARE(fixture.playback.navigationFrame(), qint64(12));
    QCOMPARE(requests.count(), 12);

    fixture.playback.play(); // must invalidate and flush pending review PCM
    QCOMPARE(fixture.playback.state(), PlayerState::Playing);
    fixture.playback.pause();
}

void TestPlaybackInteraction::automaticRangeStartMatchesManualSeekEpoch()
{
    Fixture fixture;
    QVERIFY(fixture.open(rangeStartFixture()));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);
    fixture.timeline.setViewportRange(20, 30);

    fixture.playback.seekFrame(20);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(20), 5000);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.playback.lastAudioEpochUs() >= 0, 5000);
    const qint64 manualVideoOrigin = fixture.playback.playbackOriginUs();
    const qint64 manualAudioEpoch = fixture.playback.lastAudioEpochUs();
    fixture.playback.pause();

    fixture.timeline.fitViewport();
    fixture.playback.seekFrame(80);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(80), 5000);
    fixture.timeline.setViewportRange(20, 30); // body-pan equivalent: no seek
    QSignalSpy presented(&fixture.playback, &PlaybackController::frameChanged);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(!presented.isEmpty(), 5000);
    QCOMPARE(qvariant_cast<atk::media::VideoFrame>(presented.first().at(0)).frameIndex,
             qint64(20));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.playback.lastAudioEpochUs() >= 0, 5000);
    QCOMPARE(fixture.playback.playbackOriginUs(), manualVideoOrigin);
    QCOMPARE(fixture.playback.lastAudioEpochUs(), manualAudioEpoch);

    const qint64 frameDurationUs = atk::media::ffmpeg::frameIndexToMicroseconds(
        1, AVRational{fixture.playback.metadata().frameRate.numerator,
                      fixture.playback.metadata().frameRate.denominator});
    QVERIFY(qAbs(fixture.playback.playbackOriginUs()
                 - fixture.playback.lastAudioEpochUs()) < frameDurationUs);
    fixture.playback.pause();
}

void TestPlaybackInteraction::shortRangeLoopsKeepSynchronizedEpoch()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);
    fixture.timeline.setViewportRange(20, 30);
    fixture.playback.seekFrame(20);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(20), 5000);
    fixture.playback.setLoopEnabled(true);

    int wraps = 0;
    qint64 previous = -1;
    connect(&fixture.playback, &PlaybackController::frameChanged,
            &fixture.playback, [&](const atk::media::VideoFrame& frame) {
        QVERIFY(frame.frameIndex >= 20 && frame.frameIndex <= 30);
        if (previous >= 0 && frame.frameIndex < previous) ++wraps;
        previous = frame.frameIndex;
    });
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(wraps >= 5, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.playback.lastAudioEpochUs() >= 0, 5000);
    const qint64 expectedEpoch = atk::media::ffmpeg::frameIndexToMicroseconds(
        20, AVRational{fixture.playback.metadata().frameRate.numerator,
                       fixture.playback.metadata().frameRate.denominator});
    const qint64 frameDurationUs = atk::media::ffmpeg::frameIndexToMicroseconds(
        1, AVRational{fixture.playback.metadata().frameRate.numerator,
                      fixture.playback.metadata().frameRate.denominator});
    QVERIFY(qAbs(fixture.playback.lastAudioEpochUs() - expectedEpoch) < frameDurationUs);
    fixture.playback.pause();
}

void TestPlaybackInteraction::stoppedRangeMutationReanchorsAtCurrentFrame()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.seekFrame(40);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(40), 5000);

    fixture.timeline.setViewportRange(30, 50);
    QCOMPARE(fixture.timeline.currentFrame(), qint64(40));
    QVERIFY(fixture.playback.playbackEpochDirty());

    QSignalSpy firstRun(&fixture.playback, &PlaybackController::frameChanged);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(!firstRun.isEmpty(), 5000);
    QCOMPARE(qvariant_cast<atk::media::VideoFrame>(firstRun.first().at(0)).frameIndex,
             qint64(40));
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.playback.playbackEpochDirty(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.lastAudioEpochUs(),
                              fixture.playback.playbackOriginUs(), 5000);
    fixture.playback.pause();

    const qint64 preserved = fixture.timeline.currentFrame();
    fixture.timeline.fitViewport();
    QCOMPARE(fixture.timeline.currentFrame(), preserved);
    QVERIFY(fixture.playback.playbackEpochDirty());

    QSignalSpy fitRun(&fixture.playback, &PlaybackController::frameChanged);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(!fitRun.isEmpty(), 5000);
    QCOMPARE(qvariant_cast<atk::media::VideoFrame>(fitRun.first().at(0)).frameIndex,
             preserved);
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.playback.playbackEpochDirty(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.lastAudioEpochUs(),
                              fixture.playback.playbackOriginUs(), 5000);
    fixture.playback.pause();
}

void TestPlaybackInteraction::ordinaryPauseResumeKeepsEpochClean()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.playback.lastAudioEpochUs() >= 0, 5000);
    QVERIFY(!fixture.playback.playbackEpochDirty());
    fixture.playback.pause();
    QVERIFY(!fixture.playback.playbackEpochDirty());
    fixture.playback.play();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Playing, 5000);
    QVERIFY(!fixture.playback.playbackEpochDirty());
    fixture.playback.pause();
}

void TestPlaybackInteraction::manualMediaScrubProfile_data()
{
    QTest::addColumn<QString>("fileName");

    QTest::newRow("Magic") << QStringLiteral("Magic.mp4");
    QTest::newRow("epcot") << QStringLiteral("epcot.mp4");
}

void TestPlaybackInteraction::manualMediaScrubProfile()
{
    if (manualMediaDirectory().isEmpty()) {
        QSKIP("Set ATK_MANUAL_MEDIA_DIR for the ignored local-media profile.");
    }
    QFETCH(QString, fileName);
    const QString path = QDir(manualMediaDirectory()).filePath(fileName);
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    Fixture fixture;
    QVERIFY(fixture.open(path));
    const qint64 frameCount = fixture.playback.metadata().effectiveFrameCount();
    const double frameRate = fixture.playback.metadata().frameRate.toDouble();
    QVERIFY(frameCount > 0);
    const int localStep = std::max(1, static_cast<int>(std::ceil(frameRate / 30.0)));
    const int mediumStep = std::max(2, static_cast<int>(std::ceil(frameRate / 6.0)));
    const qint64 start = frameCount / 4;
    fixture.playback.seekFrame(start);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, start, 15000);

    QElapsedTimer clock;
    clock.start();
    QHash<qint64, qint64> requestedAt;
    QVector<double> latencies;
    int requests = 0;
    int presentations = 0;
    connect(&fixture.playback, &PlaybackController::frameChanged,
            &fixture.playback, [&](const atk::media::VideoFrame& frame) {
                ++presentations;
                const auto it = requestedAt.constFind(frame.frameIndex);
                if (it != requestedAt.cend()) {
                    latencies.append((clock.nsecsElapsed() - it.value()) / 1'000'000.0);
                }
            });
    auto request = [&](qint64 frame, int waitMs) {
        requestedAt.insert(frame, clock.nsecsElapsed());
        ++requests;
        fixture.playback.scrubToFrame(frame);
        QTest::qWait(waitMs);
    };

    fixture.playback.resetReviewCacheCounters();
    fixture.playback.beginScrub();
    qint64 target = start;
    for (int i = 0; i < 60; ++i) {
        target += localStep;
        request(target, 33);
    }
    for (int i = 0; i < 60; ++i) {
        target -= localStep;
        request(target, 33);
    }
    for (int i = 0; i < 60; ++i) {
        target += mediumStep;
        request(target, 16);
    }
    for (int i = 0; i < 40; ++i) {
        target = frameCount / 10 + (frameCount * 8 / 10) * i / 39;
        request(target, 5);
    }
    const qint64 exactTarget = frameCount * 7 / 10 + 17;
    requestedAt.insert(exactTarget, clock.nsecsElapsed());
    ++requests;
    fixture.playback.endScrub(exactTarget);
    QTRY_COMPARE_WITH_TIMEOUT(
        fixture.playback.currentVideoFrame().frameIndex, exactTarget, 15000);

    const double elapsedSeconds = clock.elapsed() / 1000.0;
    const int64_t lookups = fixture.playback.reviewCacheHits()
        + fixture.playback.reviewCacheMisses();
    const double hitRate = lookups > 0
        ? 100.0 * fixture.playback.reviewCacheHits() / lookups : 0.0;
    qInfo().noquote()
        << QStringLiteral("MANUAL_PROFILE file=%1 requests=%2 presented=%3 fps=%4 "
                          "p50=%5 p90=%6 p95=%7 p99=%8 max=%9 hit=%10 exact=%11")
               .arg(fileName).arg(requests).arg(presentations)
               .arg(presentations / elapsedSeconds, 0, 'f', 2)
               .arg(percentile(latencies, 0.50), 0, 'f', 2)
               .arg(percentile(latencies, 0.90), 0, 'f', 2)
               .arg(percentile(latencies, 0.95), 0, 'f', 2)
               .arg(percentile(latencies, 0.99), 0, 'f', 2)
               .arg(percentile(latencies, 1.00), 0, 'f', 2)
               .arg(hitRate, 0, 'f', 1).arg(exactTarget);

    for (int i = 1; i <= 10; ++i) {
        const qint64 releaseTarget = frameCount * i / 11;
        fixture.playback.beginScrub();
        fixture.playback.endScrub(releaseTarget);
        QTRY_COMPARE_WITH_TIMEOUT(
            fixture.playback.currentVideoFrame().frameIndex, releaseTarget, 15000);
    }
    qInfo() << "MANUAL_EXACT_RELEASES file" << fileName << "passed" << 10;

    QSignalSpy playbackFrames(&fixture.playback, &PlaybackController::frameChanged);
    QElapsedTimer playbackElapsed;
    playbackElapsed.start();
    fixture.playback.play();
    QTest::qWait(3'500);
    const double playbackSeconds = playbackElapsed.elapsed() / 1000.0;
    qInfo().noquote()
        << QStringLiteral("MANUAL_PLAYBACK file=%1 fps=%2 drops=%3")
               .arg(fileName)
               .arg(playbackFrames.size() / playbackSeconds, 0, 'f', 2)
               .arg(fixture.playback.droppedFrameCount());
    QCOMPARE(fixture.playback.state(), PlayerState::Playing);
    QVERIFY(playbackFrames.size() / playbackSeconds > 20.0);
    fixture.playback.stop();
}

void TestPlaybackInteraction::rapidForwardPreservesInputsAndProgress()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.seekFrame(20);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(20), 5000);

    QSignalSpy frames(&fixture.playback, &PlaybackController::frameChanged);
    for (int i = 0; i < 10; ++i) {
        fixture.playback.stepForward();
    }

    QCOMPARE(fixture.playback.navigationFrame(), qint64(30));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Ready, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(30), 10000);
    QVERIFY2(frames.size() > 1, "rapid forward navigation froze until the final frame");
}

void TestPlaybackInteraction::initTestCase()
{
    if (!QFileInfo::exists(syncFixture())) {
        QSKIP("Generated sync fixture not found; build atk_test_media first.");
    }
}

void TestPlaybackInteraction::rapidForwardAndBackwardAccumulate()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    fixture.playback.seekFrame(20);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(20), 5000);

    QSignalSpy frames(&fixture.playback, &PlaybackController::frameChanged);
    QElapsedTimer commands;
    commands.start();

    for (int i = 0; i < 10; ++i) {
        fixture.playback.stepForward();
    }
    for (int i = 0; i < 3; ++i) {
        fixture.playback.stepBackward();
    }

    const qint64 commandMs = commands.elapsed();
    QCOMPARE(fixture.playback.navigationFrame(), qint64(27));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(27), 10000);
    const qint64 settleMs = commands.elapsed();

    QStringList presented;
    for (const QList<QVariant>& arguments : frames) {
        presented.append(QString::number(
            qvariant_cast<atk::media::VideoFrame>(arguments.at(0)).frameIndex));
    }
    qInfo().noquote() << "rapid commands ms" << commandMs
                      << "settle ms" << settleMs
                      << "presented" << presented.join(',');
    QVERIFY2(presented.size() > 1,
             qPrintable(QStringLiteral("navigation froze then jumped: %1")
                            .arg(presented.join(','))));
    QCOMPARE(presented.constLast(), QStringLiteral("27"));
    QCOMPARE(fixture.playback.currentFrame(), qint64(27));
}

void TestPlaybackInteraction::rapidRoundTripPreservesInputsAndProgress()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    fixture.playback.seekFrame(20);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(20), 5000);

    QSignalSpy frames(&fixture.playback, &PlaybackController::frameChanged);
    for (int i = 0; i < 10; ++i) {
        fixture.playback.stepForward();
    }
    for (int i = 0; i < 10; ++i) {
        fixture.playback.stepBackward();
    }

    QCOMPARE(fixture.playback.navigationFrame(), qint64(20));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Ready, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(20), 10000);
    QVERIFY(frames.size() > 1);
}

void TestPlaybackInteraction::scrubCoalescesToExactReleaseFrame()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    fixture.playback.beginScrub();
    for (const int64_t target : { 24, 48, 72, 96, 120, 144 }) {
        fixture.playback.scrubToFrame(target);
    }
    fixture.playback.endScrub(156);

    QCOMPARE(fixture.playback.navigationFrame(), qint64(156));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(156), 10000);
    QCOMPARE(fixture.playback.state(), PlayerState::Ready);
}

void TestPlaybackInteraction::longClipScrubProfile()
{
    if (longFixture().isEmpty() || !QFileInfo::exists(longFixture())) {
        QSKIP("Set ATK_LONG_TEST_MEDIA to the ignored 90-second development fixture.");
    }

    Fixture fixture;
    QVERIFY(fixture.open(longFixture()));
    fixture.playback.seekFrame(1000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(1000), 10000);

    fixture.playback.resetReviewCacheCounters();
    QSignalSpy localFrames(&fixture.playback, &PlaybackController::frameChanged);
    QElapsedTimer local;
    local.start();
    fixture.playback.beginScrub();
    for (qint64 frame = 1001; frame <= 1024; ++frame) {
        fixture.playback.scrubToFrame(frame);
        QTest::qWait(42);
    }
    fixture.playback.endScrub(1024);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(1024), 10000);
    const qint64 localMs = local.elapsed();
    const qsizetype localPresented = localFrames.size();

    QSignalSpy backwardFrames(&fixture.playback, &PlaybackController::frameChanged);
    fixture.playback.beginScrub();
    for (qint64 frame = 1023; frame >= 1000; --frame) {
        fixture.playback.scrubToFrame(frame);
        QTest::qWait(42);
    }
    fixture.playback.endScrub(1000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(1000), 10000);
    const qsizetype backwardPresented = backwardFrames.size();

    QSignalSpy jumpFrames(&fixture.playback, &PlaybackController::frameChanged);
    QElapsedTimer jump;
    jump.start();
    fixture.playback.beginScrub();
    for (const qint64 frame : { 200, 800, 1400, 2000 }) {
        fixture.playback.scrubToFrame(frame);
        QTest::qWait(20);
    }
    fixture.playback.endScrub(2100);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(2100), 10000);
    const qint64 jumpMs = jump.elapsed();
    const qsizetype jumpPresented = jumpFrames.size();

    const double localFps = localMs > 0
        ? localPresented * 1000.0 / static_cast<double>(localMs) : 0.0;
    const int64_t lookups = fixture.playback.reviewCacheHits()
        + fixture.playback.reviewCacheMisses();
    const double hitRate = lookups > 0
        ? 100.0 * fixture.playback.reviewCacheHits() / static_cast<double>(lookups) : 0.0;
    qInfo().noquote()
        << QStringLiteral("long scrub local %1 frames/%2 ms (%3 fps), backward %4, "
                          "jumps %5/%6 ms, cache %7/%8 MB hit %9% evict %10")
               .arg(localPresented).arg(localMs).arg(localFps, 0, 'f', 1)
               .arg(backwardPresented).arg(jumpPresented).arg(jumpMs)
               .arg(fixture.playback.reviewCacheBytes() / (1024 * 1024))
               .arg(fixture.playback.reviewCacheBudgetBytes() / (1024 * 1024))
               .arg(hitRate, 0, 'f', 1).arg(fixture.playback.reviewCacheEvictions());

    QVERIFY(localPresented >= 18);
    QVERIFY(backwardPresented >= 18);
    QVERIFY(jumpPresented >= 2);
    QVERIFY(fixture.playback.reviewCacheBytes()
            <= fixture.playback.reviewCacheBudgetBytes());
}

void TestPlaybackInteraction::explicitSeekResetsLogicalTarget()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    for (int i = 0; i < 8; ++i) {
        fixture.playback.stepForward();
    }
    QCOMPARE(fixture.playback.navigationFrame(), qint64(8));

    fixture.playback.seekFrame(100);
    QCOMPARE(fixture.playback.navigationFrame(), qint64(100));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(100), 10000);
}

void TestPlaybackInteraction::realTimePlaybackCrossesLoopBoundary()
{
    Fixture fixture;
    QVERIFY(fixture.open());
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.currentVideoFrame().frameIndex, qint64(0), 5000);

    fixture.playback.setMuted(true);
    fixture.playback.setLoopEnabled(true);
    QSignalSpy frames(&fixture.playback, &PlaybackController::frameChanged);
    int loopCrossings = 0;
    int64_t previousFrame = -1;
    connect(&fixture.playback, &PlaybackController::frameChanged,
            &fixture.playback, [&](const atk::media::VideoFrame& frame) {
                if (previousFrame >= 0 && frame.frameIndex < previousFrame) {
                    ++loopCrossings;
                }
                previousFrame = frame.frameIndex;
            });
    QElapsedTimer elapsed;
    elapsed.start();
    fixture.playback.play();

    // The fixture is ten seconds. Run beyond two complete cycles and verify
    // both observed end-to-start transitions rather than inferring looping
    // from the final playhead position.
    QTest::qWait(21'500);

    const double seconds = elapsed.elapsed() / 1000.0;
    const double observedFps = frames.count() / seconds;
    qInfo().noquote()
        << QStringLiteral("Release-observable playback %1 fps, drops %2, loops %3, frame %4")
               .arg(observedFps, 0, 'f', 2)
               .arg(fixture.playback.droppedFrameCount())
               .arg(loopCrossings)
               .arg(fixture.playback.currentFrame());
    QCOMPARE(fixture.playback.state(), PlayerState::Playing);
    QVERIFY(fixture.playback.currentFrame() >= 2);
    QVERIFY(fixture.playback.currentFrame() < 50);
    QVERIFY(observedFps > 20.0);
    QVERIFY(fixture.playback.droppedFrameCount() < 12);
    QVERIFY(loopCrossings >= 2);
    fixture.playback.stop();
}

QTEST_GUILESS_MAIN(TestPlaybackInteraction)
#include "tst_playbackinteraction.moc"
