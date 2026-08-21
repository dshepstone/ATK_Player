#include "playback/PlaybackController.h"

#include "timeline/TimelineModel.h"

#include <QSignalSpy>
#include <QTest>

using atk::media::FrameRate;
using atk::playback::PlaybackController;
using atk::playback::PlayerState;
using atk::timeline::TimelineModel;

/// Covers the Phase 0 transport behaviour: state transitions, frame stepping
/// against the placeholder extent, and the clamping that stops the placeholder
/// frame counter from ever going out of range.
class TestPlaybackController : public QObject {
    Q_OBJECT

private:
    struct Fixture {
        TimelineModel timeline;
        PlaybackController playback{ &timeline };

        /// Mirrors what MainWindow installs at startup.
        explicit Fixture(int64_t frames = 100, int fps = 24)
        {
            if (frames > 0) {
                timeline.setPlaceholderExtent(frames, FrameRate::fromInteger(fps));
            }
        }
    };

private slots:
    void startsReady();
    void playThenPauseTogglesState();
    void togglePlayPauseAlternates();
    void stateChangedIsNotEmittedTwice();

    void stepForwardAdvancesOneFrame();
    void stepBackwardRetreatsOneFrame();
    void stepBackwardCannotGoBelowZero();
    void stepForwardCannotPassLastFrame();
    void steppingLeavesPlayMode();
    void rapidStepsAccumulateLogicalTarget();
    void alternatingRapidStepsPreserveEveryInput();
    void scrubFinalTargetWins();
    void seekAndPlayResetLogicalTarget();

    void goToStartReturnsToFirstFrame();
    void goToEndJumpsToLastFrame();
    void seekClampsToExtent();

    void loopToggles();
    void loopEmitsOnlyOnChange();

    void rangeConstrainsStepping();
    void clearRangeRestoresFullExtent();
    void reviewRangeStopsOnInclusiveEnd();
    void playOutsideRangeRestartsAtStart();
    void loopWrapsWithoutEscapingRange();
    void activeRangeChangeWhilePlayingIsImmediate();

    void placeholderExtentIsMarkedAsSuch();
    void hasNoMediaInPhaseZero();
    void transportIsInertWithoutAnExtent();
    void frameStepAudioIsOffAndIndependentByDefault();
    void timestampSkipClampsToReviewRange();
};

void TestPlaybackController::timestampSkipClampsToReviewRange()
{
    Fixture fixture(1000, 24);
    fixture.timeline.setViewportRange(100, 500);
    fixture.playback.seekFrame(300);
    fixture.playback.skipBySeconds(10);
    QCOMPARE(fixture.playback.navigationFrame(), qint64(500));
    fixture.playback.skipBySeconds(-10);
    QCOMPARE(fixture.playback.navigationFrame(), qint64(260));
    fixture.playback.skipBySeconds(-100);
    QCOMPARE(fixture.playback.navigationFrame(), qint64(100));
}

void TestPlaybackController::startsReady()
{
    Fixture fixture;
    // A placeholder extent is a real extent to move along, so the transport is
    // Ready even though no file is open. hasMedia() is what distinguishes them.
    QCOMPARE(fixture.playback.state(), PlayerState::Ready);
    QVERIFY(!fixture.playback.isPlaying());
    QVERIFY(!fixture.playback.hasMedia());
    QCOMPARE(fixture.playback.currentFrame(), qint64(0));
}

void TestPlaybackController::frameStepAudioIsOffAndIndependentByDefault()
{
    Fixture fixture;
    QVERIFY(fixture.playback.isAudioScrubEnabled());
    QVERIFY(!fixture.playback.isFrameStepAudioEnabled());
    fixture.playback.setFrameStepAudioEnabled(true);
    QVERIFY(fixture.playback.isFrameStepAudioEnabled());
    QVERIFY(fixture.playback.isAudioScrubEnabled());
    fixture.playback.setAudioScrubEnabled(false);
    QVERIFY(fixture.playback.isFrameStepAudioEnabled());
}

void TestPlaybackController::playThenPauseTogglesState()
{
    Fixture fixture;

    fixture.playback.play();
    QCOMPARE(fixture.playback.state(), PlayerState::Playing);
    QVERIFY(fixture.playback.isPlaying());

    fixture.playback.pause();
    QCOMPARE(fixture.playback.state(), PlayerState::Paused);
    QVERIFY(!fixture.playback.isPlaying());
}

void TestPlaybackController::togglePlayPauseAlternates()
{
    Fixture fixture;

    fixture.playback.togglePlayPause();
    QVERIFY(fixture.playback.isPlaying());

    fixture.playback.togglePlayPause();
    QVERIFY(!fixture.playback.isPlaying());

    fixture.playback.togglePlayPause();
    QVERIFY(fixture.playback.isPlaying());
}

void TestPlaybackController::stateChangedIsNotEmittedTwice()
{
    Fixture fixture;
    QSignalSpy spy(&fixture.playback, &PlaybackController::stateChanged);

    fixture.playback.play();
    QCOMPARE(spy.count(), 1);

    // Already playing: no transition, so no signal.
    fixture.playback.play();
    QCOMPARE(spy.count(), 1);
}

void TestPlaybackController::stepForwardAdvancesOneFrame()
{
    Fixture fixture;
    fixture.playback.seekFrame(10);

    fixture.playback.stepForward();
    QCOMPARE(fixture.playback.currentFrame(), qint64(11));
}

void TestPlaybackController::stepBackwardRetreatsOneFrame()
{
    Fixture fixture;
    fixture.playback.seekFrame(10);

    fixture.playback.stepBackward();
    QCOMPARE(fixture.playback.currentFrame(), qint64(9));
}

void TestPlaybackController::stepBackwardCannotGoBelowZero()
{
    Fixture fixture;
    QCOMPARE(fixture.playback.currentFrame(), qint64(0));

    // Holding the left arrow at frame 0 must not produce a negative frame.
    fixture.playback.stepBackward();
    fixture.playback.stepBackward();
    QCOMPARE(fixture.playback.currentFrame(), qint64(0));
}

void TestPlaybackController::stepForwardCannotPassLastFrame()
{
    Fixture fixture;
    fixture.playback.goToEnd();
    QCOMPARE(fixture.playback.currentFrame(), qint64(99));

    fixture.playback.stepForward();
    fixture.playback.stepForward();
    QCOMPARE(fixture.playback.currentFrame(), qint64(99));
}

void TestPlaybackController::steppingLeavesPlayMode()
{
    Fixture fixture;
    fixture.playback.play();
    QVERIFY(fixture.playback.isPlaying());

    // Stepping is a deliberate single-frame move, so it stops playback rather
    // than fighting the clock for the playhead.
    fixture.playback.stepForward();
    QCOMPARE(fixture.playback.state(), PlayerState::Paused);
}

void TestPlaybackController::rapidStepsAccumulateLogicalTarget()
{
    Fixture fixture;
    fixture.playback.seekFrame(10);

    for (int i = 0; i < 10; ++i) {
        fixture.playback.stepForward();
    }

    QCOMPARE(fixture.playback.navigationFrame(), qint64(20));
    QCOMPARE(fixture.playback.currentFrame(), qint64(20));
}

void TestPlaybackController::alternatingRapidStepsPreserveEveryInput()
{
    Fixture fixture;
    fixture.playback.seekFrame(10);

    for (int i = 0; i < 10; ++i) {
        fixture.playback.stepForward();
    }
    for (int i = 0; i < 3; ++i) {
        fixture.playback.stepBackward();
    }

    QCOMPARE(fixture.playback.navigationFrame(), qint64(17));
    QCOMPARE(fixture.playback.currentFrame(), qint64(17));
}

void TestPlaybackController::scrubFinalTargetWins()
{
    Fixture fixture;
    fixture.playback.beginScrub();
    for (const int64_t frame : { 20, 30, 45, 70 }) {
        fixture.playback.scrubToFrame(frame);
    }
    fixture.playback.endScrub(63);

    QCOMPARE(fixture.playback.navigationFrame(), qint64(63));
    QCOMPARE(fixture.playback.currentFrame(), qint64(63));
    QCOMPARE(fixture.playback.state(), PlayerState::Ready);
}

void TestPlaybackController::seekAndPlayResetLogicalTarget()
{
    Fixture fixture;
    fixture.playback.seekFrame(10);
    fixture.playback.stepForward();
    fixture.playback.stepForward();
    QCOMPARE(fixture.playback.navigationFrame(), qint64(12));

    fixture.playback.seekFrame(50);
    QCOMPARE(fixture.playback.navigationFrame(), qint64(50));

    fixture.playback.play();
    QCOMPARE(fixture.playback.navigationFrame(), qint64(50));
}

void TestPlaybackController::goToStartReturnsToFirstFrame()
{
    Fixture fixture;
    fixture.playback.seekFrame(42);
    QCOMPARE(fixture.playback.currentFrame(), qint64(42));

    fixture.playback.goToStart();
    QCOMPARE(fixture.playback.currentFrame(), qint64(0));
}

void TestPlaybackController::goToEndJumpsToLastFrame()
{
    Fixture fixture;
    fixture.playback.goToEnd();
    QCOMPARE(fixture.playback.currentFrame(), qint64(99));
}

void TestPlaybackController::seekClampsToExtent()
{
    Fixture fixture;

    fixture.playback.seekFrame(5000);
    QCOMPARE(fixture.playback.currentFrame(), qint64(99));

    fixture.playback.seekFrame(-5000);
    QCOMPARE(fixture.playback.currentFrame(), qint64(0));
}

void TestPlaybackController::loopToggles()
{
    Fixture fixture;
    QVERIFY(!fixture.playback.isLoopEnabled());

    fixture.playback.setLoopEnabled(true);
    QVERIFY(fixture.playback.isLoopEnabled());

    fixture.playback.setLoopEnabled(false);
    QVERIFY(!fixture.playback.isLoopEnabled());
}

void TestPlaybackController::loopEmitsOnlyOnChange()
{
    Fixture fixture;
    QSignalSpy spy(&fixture.playback, &PlaybackController::loopEnabledChanged);

    fixture.playback.setLoopEnabled(true);
    QCOMPARE(spy.count(), 1);

    fixture.playback.setLoopEnabled(true);
    QCOMPARE(spy.count(), 1);
}

void TestPlaybackController::rangeConstrainsStepping()
{
    Fixture fixture;
    fixture.playback.setPlaybackRange(10, 20);

    fixture.playback.goToStart();
    QCOMPARE(fixture.playback.currentFrame(), qint64(10));

    fixture.playback.goToEnd();
    QCOMPARE(fixture.playback.currentFrame(), qint64(20));

    // Stepping past the out point must stay on it.
    fixture.playback.stepForward();
    QCOMPARE(fixture.playback.currentFrame(), qint64(20));
}

void TestPlaybackController::clearRangeRestoresFullExtent()
{
    Fixture fixture;
    fixture.playback.setPlaybackRange(10, 20);
    fixture.playback.clearPlaybackRange();

    fixture.playback.goToEnd();
    QCOMPARE(fixture.playback.currentFrame(), qint64(99));
}

void TestPlaybackController::reviewRangeStopsOnInclusiveEnd()
{
    Fixture fixture(100, 100);
    fixture.timeline.setViewportRange(20, 30);
    fixture.playback.seekFrame(25);
    QSignalSpy frames(&fixture.timeline, &TimelineModel::currentFrameChanged);
    fixture.playback.play();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Ended, 1500);
    QCOMPARE(fixture.playback.currentFrame(), qint64(30));
    bool presentedEnd = false;
    for (const auto& change : frames) {
        const qint64 frame = change.at(0).toLongLong();
        QVERIFY2(frame <= 30, "Playback escaped the inclusive review end");
        presentedEnd |= frame == 30;
    }
    QVERIFY(presentedEnd);

    fixture.playback.play();
    QCOMPARE(fixture.playback.currentFrame(), qint64(20));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Ended, 1500);
    QCOMPARE(fixture.playback.currentFrame(), qint64(30));
}

void TestPlaybackController::playOutsideRangeRestartsAtStart()
{
    Fixture fixture(100, 100);
    fixture.timeline.setViewportRange(20, 30);
    fixture.timeline.setCurrentFrame(80);
    fixture.playback.play();
    QCOMPARE(fixture.playback.currentFrame(), qint64(20));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.playback.state(), PlayerState::Ended, 1500);
}

void TestPlaybackController::loopWrapsWithoutEscapingRange()
{
    Fixture fixture(100, 100);
    fixture.timeline.setViewportRange(20, 30);
    fixture.timeline.setCurrentFrame(20);
    fixture.playback.setLoopEnabled(true);
    QSignalSpy frames(&fixture.timeline, &TimelineModel::currentFrameChanged);
    fixture.playback.play();
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        int starts = 0;
        for (const auto& change : frames) starts += change.at(0).toLongLong() == 20;
        return starts >= 3;
    }(), 2500);
    fixture.playback.pause();
    bool presentedEnd = false;
    for (const auto& change : frames) {
        const qint64 frame = change.at(0).toLongLong();
        QVERIFY(frame >= 20 && frame <= 30);
        presentedEnd |= frame == 30;
    }
    QVERIFY(presentedEnd);
}

void TestPlaybackController::activeRangeChangeWhilePlayingIsImmediate()
{
    Fixture loopOff(100, 50);
    loopOff.timeline.setViewportRange(10, 60);
    loopOff.timeline.setCurrentFrame(40);
    loopOff.playback.play();
    loopOff.timeline.setViewportRange(10, 30);
    QTRY_COMPARE_WITH_TIMEOUT(loopOff.playback.currentFrame(), qint64(30), 500);
    QCOMPARE(loopOff.playback.state(), PlayerState::Ended);

    Fixture loopOn(100, 50);
    loopOn.timeline.setViewportRange(10, 60);
    loopOn.timeline.setCurrentFrame(40);
    loopOn.playback.setLoopEnabled(true);
    loopOn.playback.play();
    loopOn.timeline.setViewportRange(10, 30);
    QTRY_VERIFY_WITH_TIMEOUT(loopOn.playback.currentFrame() >= 10
                            && loopOn.playback.currentFrame() <= 30, 500);
    QVERIFY(loopOn.playback.isPlaying());
    loopOn.playback.pause();
}

void TestPlaybackController::placeholderExtentIsMarkedAsSuch()
{
    Fixture fixture;
    // The UI relies on this flag to say "no media" while still showing frame
    // numbers, so it must survive ordinary transport use.
    QVERIFY(fixture.timeline.isPlaceholder());

    fixture.playback.stepForward();
    fixture.playback.setLoopEnabled(true);
    QVERIFY(fixture.timeline.isPlaceholder());

    // A real extent clears it.
    fixture.timeline.setFrameCount(240);
    QVERIFY(!fixture.timeline.isPlaceholder());
}

void TestPlaybackController::hasNoMediaInPhaseZero()
{
    Fixture fixture;
    // A placeholder extent must never be mistaken for an open file.
    QVERIFY(!fixture.playback.hasMedia());
}

void TestPlaybackController::transportIsInertWithoutAnExtent()
{
    Fixture fixture(0);
    QCOMPARE(fixture.timeline.frameCount(), qint64(0));
    QCOMPARE(fixture.playback.state(), PlayerState::Empty);

    // Nothing to play and nothing to step to: play() must not claim otherwise.
    fixture.playback.play();
    QCOMPARE(fixture.playback.state(), PlayerState::Empty);

    fixture.playback.stepForward();
    QCOMPARE(fixture.playback.currentFrame(), qint64(0));
}

QTEST_GUILESS_MAIN(TestPlaybackController)
#include "tst_playbackcontroller.moc"
