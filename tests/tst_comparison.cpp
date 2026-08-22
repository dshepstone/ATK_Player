#include "core/commands/CommandDefinitions.h"
#include "media/MediaSource.h"
#include "media/CompareAudioWorker.h"
#include "playback/CompareSession.h"
#include "playback/CompareVideoLane.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/MainWindow.h"
#include "ui/TimelineWidget.h"
#include "ui/ViewerWidget.h"

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QToolButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using atk::playback::CompareLayout;
using atk::playback::ComparePane;
using atk::playback::CompareSession;
using atk::playback::PlayerState;

namespace {
QString media(const char* name)
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/") + QString::fromLatin1(name);
}

QString writeProject(QTemporaryDir& directory)
{
    atk::project::Project project;
    project.setName(QStringLiteral("Comparison test"));
    for (const auto& item : {qMakePair(QStringLiteral("A 24"), media("atk_fixture_48f.mkv")),
                             qMakePair(QStringLiteral("B 59.94"), media("atk_compare_5994fps.mkv")),
                             qMakePair(QStringLiteral("C 30"), media("atk_compare_30fps.mkv"))}) {
        project.addSource(std::make_shared<atk::media::MediaSource>(item.second));
        project.mutableEntries().last().displayName = item.first;
    }
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("comparison.atkproj"));
    return atk::project::ProjectSerializer::save(project, path).ok ? path : QString();
}

QAction* command(atk::ui::MainWindow& window, const char* key)
{
    return window.findChild<QAction*>(QString::fromLatin1(key));
}
}

class TestComparison : public QObject {
    Q_OBJECT
private slots:
    void timestampMappingHasNoCumulativeDrift();
    void vfrPresentationOrderAndClamping();
    void commandDefaultsAreShortcutSafe();
    void enableLayoutActiveViewerAndExitAreNonDestructive();
    void unequalRateLaneTracksAuthoritativeSeek();
    void comparisonEndDoesNotAdvancePlaylist();
    void compareAudioModesAreTransientAndMapped();
    void externalAudioValidationAndSilentMode();
    void selectedAudioControlsWaveformMapping();
    void firstExternalWaveformIsAuthoritative();
    void audioModeChangeCannotPinVisualPlayhead();
    void playFromComparisonEndRestartsActiveRange();
    void compareBarHasGroupedControlHierarchy();
};

void TestComparison::timestampMappingHasNoCumulativeDrift()
{
    const atk::media::FrameRate a{24000, 1001};
    const atk::media::FrameRate b{60000, 1001};
    for (qint64 aFrame = 0; aFrame < 24 * 60 * 10; aFrame += 37) {
        const qint64 target = CompareSession::frameTimeUs(aFrame, a);
        const qint64 bFrame = CompareSession::constantRateFrameForTime(target, b, 0, 100000);
        const qint64 actual = CompareSession::frameTimeUs(bFrame, b);
        QVERIFY2(actual <= target, "comparison frame must not lead the authoritative timestamp");
        QVERIFY2(target - actual <= CompareSession::frameTimeUs(1, b),
                 "unequal-rate error must remain bounded by one B frame");
    }
}

void TestComparison::vfrPresentationOrderAndClamping()
{
    const QVector<qint64> pts{0, 40'000, 85'000, 119'000, 200'000};
    QCOMPARE(CompareSession::frameForPts(-1, pts), 0);
    QCOMPARE(CompareSession::frameForPts(84'999, pts), 1);
    QCOMPARE(CompareSession::frameForPts(119'000, pts), 3);
    QCOMPARE(CompareSession::mappedTargetUs(5'000'000, 1'000'000, 2'000'000, 4'000'000), 4'000'000);
    QCOMPARE(CompareSession::mappedTargetUs(500'000, 1'000'000, 2'000'000, 4'000'000), 2'000'000);
    QCOMPARE(CompareSession::mappedTargetUs(2'000'000, 1'000'000, 2'000'000, 4'000'000, -250'000), 2'750'000);
}

void TestComparison::commandDefaultsAreShortcutSafe()
{
    const auto* toggle = atk::commands::find(atk::commands::CommandId::ToggleComparison);
    const auto* side = atk::commands::find(atk::commands::CommandId::CompareSideBySide);
    const auto* stacked = atk::commands::find(atk::commands::CommandId::CompareStacked);
    QVERIFY(toggle && side && stacked);
    QVERIFY(!toggle->defaultShortcut && !side->defaultShortcut && !stacked->defaultShortcut);
}

void TestComparison::enableLayoutActiveViewerAndExitAreNonDestructive()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    const bool modified = window.project()->isModified();
    const int activeIndex = window.project()->activeIndex();
    QAction* toggle = command(window, "view.toggleComparison");
    QVERIFY(toggle && toggle->isEnabled());
    toggle->trigger();
    QTRY_VERIFY(window.isComparisonActive());
    QVERIFY(window.viewerA()->isVisible());
    QVERIFY(window.viewerB()->isVisible());
    QVERIFY(window.compareSession()->sourceAId() != window.compareSession()->sourceBId());
    QCOMPARE(window.project()->isModified(), modified);

    command(window, "view.compareStacked")->trigger();
    QCOMPARE(window.compareSession()->layout(), CompareLayout::Stacked);
    QCOMPARE(window.project()->activeIndex(), activeIndex);
    QTest::mouseClick(window.viewerB(), Qt::LeftButton);
    QCOMPARE(window.compareSession()->activePane(), ComparePane::B);

    QAction* videoFullscreen = command(window, "view.toggleVideoFullScreen");
    QVERIFY(videoFullscreen && !videoFullscreen->isEnabled());
    toggle->trigger();
    QVERIFY(!window.isComparisonActive());
    QVERIFY(videoFullscreen->isEnabled());
    QCOMPARE(window.project()->activeIndex(), activeIndex);
    QCOMPARE(window.project()->isModified(), modified);
}

void TestComparison::unequalRateLaneTracksAuthoritativeSeek()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    QTRY_VERIFY(window.compareVideoLane() && window.compareVideoLane()->isReady());
    QSignalSpy bFrames(window.compareVideoLane(), &atk::playback::CompareVideoLane::frameChanged);
    window.playbackController()->seekFrame(24);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(24), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!bFrames.isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.compareVideoLane()->presentedPtsUs() >= 950'000, 5000);
    QVERIFY(window.compareVideoLane()->presentedPtsUs() <= 1'050'000);
    QVERIFY(window.compareVideoLane()->cacheBytes() <= window.compareVideoLane()->cacheBudgetBytes());
}

void TestComparison::comparisonEndDoesNotAdvancePlaylist()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    window.playbackController()->play();
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ended, 10000);
    QCOMPARE(window.project()->activeIndex(), 0);
    QVERIFY(window.isComparisonActive());
}

void TestComparison::compareAudioModesAreTransientAndMapped()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    QTRY_VERIFY(window.isComparisonActive());
    const bool modified = window.project()->isModified();
    auto* sourceB = window.findChild<QComboBox*>(QStringLiteral("CompareSourceB"));
    auto* audioMode = window.findChild<QComboBox*>(QStringLiteral("CompareAudioMode"));
    QVERIFY(sourceB && audioMode);
    sourceB->setCurrentIndex(2); // generated 30 fps / 44.1 kHz source
    QTRY_COMPARE(window.compareSession()->sourceBId(), window.project()->entries().at(2).id);
    audioMode->setCurrentIndex(1);
    QTRY_COMPARE(window.compareSession()->audioMode(), atk::playback::CompareAudioMode::SourceB);
    QVERIFY(window.playbackController()->hasComparisonAudioOverride());
    QVERIFY(window.playbackController()->comparisonAudioGeneration() > 0);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->comparisonAudioAvailable(), 5000);
    QCOMPARE(window.project()->isModified(), modified);

    window.compareSession()->setSourceBOffsetUs(250'000);
    audioMode->setCurrentIndex(0);
    audioMode->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->comparisonAudioAvailable(), 5000);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->lastComparisonAudioTargetUs() >= 250'000, 5000);
    window.playbackController()->pause();

    QSignalSpy reviewAudio(window.playbackController(),
                           &atk::playback::PlaybackController::reviewAudioRequested);
    window.playbackController()->setFrameStepAudioEnabled(true);
    window.playbackController()->stepForward();
    QTRY_VERIFY_WITH_TIMEOUT(!reviewAudio.isEmpty(), 5000);
    const qint64 requestedUs = reviewAudio.last().at(0).toLongLong();
    QVERIFY(requestedUs >= 250'000);

    audioMode->setCurrentIndex(0);
    QTRY_COMPARE(window.compareSession()->audioMode(), atk::playback::CompareAudioMode::SourceA);
    QVERIFY(!window.playbackController()->hasComparisonAudioOverride());
    QCOMPARE(window.project()->isModified(), modified);
}

void TestComparison::externalAudioValidationAndSilentMode()
{
    QString error;
    QVERIFY(atk::media::CompareAudioWorker::validateSource(media("atk_external_32k.wav"), &error));
    QVERIFY(!atk::media::CompareAudioWorker::validateSource(media("atk_compare_5994fps.mkv"), &error));

    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    auto* audioMode = window.findChild<QComboBox*>(QStringLiteral("CompareAudioMode"));
    auto* label = window.findChild<QLabel*>(QStringLiteral("CompareExternalAudioName"));
    QVERIFY(audioMode && label);
    window.compareSession()->setExternalAudioPath(media("atk_external_32k.wav"));
    audioMode->setCurrentIndex(2);
    QTRY_COMPARE(window.compareSession()->audioMode(), atk::playback::CompareAudioMode::External);
    QVERIFY(window.playbackController()->hasComparisonAudioOverride());
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->comparisonAudioAvailable(), 5000);
    QVERIFY(label->text().contains(QStringLiteral("atk_external_32k")));
    command(window, "file.saveProject")->trigger();
    QFile saved(window.project()->filePath()); QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray json = saved.readAll();
    QVERIFY(!json.contains("externalAudio"));
    QVERIFY(!json.contains("compareAudio"));
    window.compareSession()->setExternalAudioPath(QString());
    audioMode->setCurrentIndex(0);
    audioMode->setCurrentIndex(2);
    QTRY_VERIFY(label->text().contains(QStringLiteral("No External")));
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    window.playbackController()->pause();
}

void TestComparison::selectedAudioControlsWaveformMapping()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    auto* sourceB = window.findChild<QComboBox*>(QStringLiteral("CompareSourceB"));
    auto* audioMode = window.findChild<QComboBox*>(QStringLiteral("CompareAudioMode"));
    auto* timeline = window.findChild<atk::ui::TimelineWidget*>(QStringLiteral("TimelineWidget"));
    QVERIFY(sourceB && audioMode && timeline);

    const quint64 aGeneration = window.playbackController()->waveformGeneration();
    QCOMPARE(timeline->waveformTimeOffsetUs(), qint64(0));

    sourceB->setCurrentIndex(2); // generated 30 fps / 44.1 kHz source
    QTRY_COMPARE(window.compareSession()->sourceBId(), window.project()->entries().at(2).id);
    window.compareSession()->setSourceBOffsetUs(250'000);
    audioMode->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveformGeneration() > aGeneration, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveform().isComplete(), 5000);
    QCOMPARE(timeline->waveformTimeOffsetUs(), qint64(250'000));
    QCOMPARE(window.playbackController()->waveformTimeOffsetUs(), qint64(250'000));
    QCOMPARE(window.playbackController()->waveformSourcePath(), media("atk_compare_30fps.mkv"));
    QVERIFY(!window.playbackController()->waveform().isEmpty());

    const quint64 bGeneration = window.playbackController()->waveformGeneration();
    window.compareSession()->setExternalAudioPath(media("atk_external_32k.wav"));
    window.compareSession()->setExternalAudioOffsetUs(500'000);
    audioMode->setCurrentIndex(2);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveformGeneration() > bGeneration, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveform().isComplete(), 5000);
    QCOMPARE(timeline->waveformTimeOffsetUs(), qint64(500'000));
    QCOMPARE(window.playbackController()->waveformTimeOffsetUs(), qint64(500'000));
    QCOMPARE(window.playbackController()->waveformSourcePath(), media("atk_external_32k.wav"));
    QVERIFY(window.playbackController()->waveform().coveredUs() >= 2'900'000);
    QVERIFY(window.playbackController()->waveform().coveredUs() <= 3'100'000);

    const quint64 externalGeneration = window.playbackController()->waveformGeneration();
    window.compareSession()->setExternalAudioPath(QString());
    audioMode->setCurrentIndex(0);
    audioMode->setCurrentIndex(2);
    QTRY_VERIFY(window.playbackController()->waveformGeneration() > externalGeneration);
    QVERIFY(window.playbackController()->waveform().isEmpty());
    QCOMPARE(timeline->waveformTimeOffsetUs(), qint64(500'000));

    audioMode->setCurrentIndex(1);
    sourceB->setCurrentIndex(1); // generated video-only source
    QTRY_COMPARE(window.compareSession()->sourceBId(), window.project()->entries().at(1).id);
    QTRY_VERIFY(window.playbackController()->waveform().isEmpty());
}

void TestComparison::firstExternalWaveformIsAuthoritative()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveform().isComplete(), 5000);
    command(window, "view.toggleComparison")->trigger();
    auto* audioMode = window.findChild<QComboBox*>(QStringLiteral("CompareAudioMode"));
    QVERIFY(audioMode);

    // The review fixture has deliberately silent and active regions, unlike A's
    // continuous tone. This makes an accidentally retained A waveform fail.
    const QString external = media("atk_review_10s.mkv");
    window.compareSession()->setExternalAudioPath(external);
    audioMode->setCurrentIndex(2);
    QTRY_COMPARE(window.playbackController()->waveformSourcePath(), external);
    QCOMPARE(window.playbackController()->waveformSelectionGeneration(),
             window.compareSession()->generation());
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveform().isComplete(), 5000);
    const auto& first = window.playbackController()->waveform();
    QVERIFY(first.peakOverRange(0, 100'000, 300'000).isSilent());
    QVERIFY(first.peakOverRange(0, 1'600'000, 1'900'000).amplitude() > 0.01f);
    const auto firstBuckets = first.level(0);

    audioMode->setCurrentIndex(0);
    QTRY_COMPARE(window.playbackController()->waveformSourcePath(), media("atk_fixture_48f.mkv"));
    audioMode->setCurrentIndex(2);
    QTRY_COMPARE(window.playbackController()->waveformSourcePath(), external);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveform().isComplete(), 5000);
    const auto secondBuckets = window.playbackController()->waveform().level(0);
    QCOMPARE(secondBuckets.size(), firstBuckets.size());
    for (qsizetype i = 0; i < firstBuckets.size(); i += 97) {
        QCOMPARE(secondBuckets.at(i).minimum, firstBuckets.at(i).minimum);
        QCOMPARE(secondBuckets.at(i).maximum, firstBuckets.at(i).maximum);
    }

    // Rapid replacements must settle on the last immutable request snapshot.
    window.compareSession()->setExternalAudioPath(media("atk_external_32k.wav"));
    audioMode->setCurrentIndex(0); audioMode->setCurrentIndex(2);
    window.compareSession()->setExternalAudioPath(external);
    audioMode->setCurrentIndex(0); audioMode->setCurrentIndex(2);
    window.compareSession()->setExternalAudioPath(media("atk_external_32k.wav"));
    audioMode->setCurrentIndex(0); audioMode->setCurrentIndex(2);
    QTRY_COMPARE(window.playbackController()->waveformSourcePath(), media("atk_external_32k.wav"));
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->waveform().isComplete(), 5000);
    QVERIFY(window.playbackController()->waveform().coveredUs() < 3'100'000);
}

void TestComparison::audioModeChangeCannotPinVisualPlayhead()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    auto* sourceB = window.findChild<QComboBox*>(QStringLiteral("CompareSourceB"));
    auto* audioMode = window.findChild<QComboBox*>(QStringLiteral("CompareAudioMode"));
    auto* timeline = window.findChild<atk::ui::TimelineWidget*>(QStringLiteral("TimelineWidget"));
    QVERIFY(sourceB && audioMode && timeline);
    sourceB->setCurrentIndex(2);
    QTRY_COMPARE(window.compareSession()->sourceBId(), window.project()->entries().at(2).id);

    // Reproduce the visual-only stale overlay: release a scrub on the already
    // current first frame, which does not make TimelineModel emit a change.
    const QPoint firstFrame(timeline->positionForFrame(0), timeline->height() - 18);
    QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, firstFrame);
    QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, firstFrame);
    QCOMPARE(timeline->displayedFrame(), qint64(0));

    audioMode->setCurrentIndex(1);
    command(window, "playback.playPause")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->currentFrame() > 2, 5000);
    QTRY_COMPARE(timeline->displayedFrame(), window.playbackController()->currentFrame());
    window.playbackController()->pause();

    window.playbackController()->seekFrame(10);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(10), 5000);
    window.compareSession()->setExternalAudioPath(media("atk_external_32k.wav"));
    audioMode->setCurrentIndex(2);
    command(window, "playback.playPause")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->currentFrame() > 10, 5000);
    QVERIFY(window.playbackController()->currentFrame() < 30);

    qint64 previous = window.playbackController()->currentFrame();
    for (int index : {0, 1, 2, 0, 1}) {
        audioMode->setCurrentIndex(index);
        QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->currentFrame() > previous, 3000);
        QCOMPARE(timeline->displayedFrame(), window.playbackController()->currentFrame());
        previous = window.playbackController()->currentFrame();
    }
    window.playbackController()->pause();
}

void TestComparison::playFromComparisonEndRestartsActiveRange()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();
    QTRY_VERIFY(window.compareVideoLane() && window.compareVideoLane()->isReady());

    window.playbackController()->setPlaybackRange(5, 15);
    window.playbackController()->seekFrame(15);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(15), 5000);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->currentFrame() >= 5
                             && window.playbackController()->currentFrame() < 15, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.compareVideoLane()->presentedPtsUs() < 500'000, 5000);
    window.playbackController()->pause();

    auto* sourceB = window.findChild<QComboBox*>(QStringLiteral("CompareSourceB"));
    auto* audioMode = window.findChild<QComboBox*>(QStringLiteral("CompareAudioMode"));
    sourceB->setCurrentIndex(2);
    window.compareSession()->setSourceBOffsetUs(250'000);
    audioMode->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->comparisonAudioAvailable(), 5000);
    window.playbackController()->seekFrame(15);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(15), 5000);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->lastComparisonAudioTargetUs() >= 250'000, 5000);
    QVERIFY(window.playbackController()->lastComparisonAudioTargetUs() < 500'000);
    window.playbackController()->pause();
}

void TestComparison::compareBarHasGroupedControlHierarchy()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "view.toggleComparison")->trigger();

    for (const auto& name : {"CompareSourcesSection", "CompareAudioSection", "CompareLayoutSection"})
        QVERIFY(window.findChild<QLabel*>(QString::fromLatin1(name)));
    auto* load = window.findChild<QToolButton*>(QStringLiteral("CompareLoadExternalAudio"));
    auto* clear = window.findChild<QToolButton*>(QStringLiteral("CompareClearExternalAudio"));
    auto* side = window.findChild<QToolButton*>(QStringLiteral("CompareSideBySideButton"));
    auto* stacked = window.findChild<QToolButton*>(QStringLiteral("CompareStackedButton"));
    auto* bar = window.findChild<QWidget*>(QStringLiteral("CompareBar"));
    QVERIFY(load && clear && side && stacked && bar);
    QCOMPARE(load->text(), QStringLiteral("Load…"));
    QCOMPARE(clear->text(), QStringLiteral("Clear"));
    QVERIFY(side->isChecked());
    stacked->click();
    QCOMPARE(window.compareSession()->layout(), CompareLayout::Stacked);
    QVERIFY(stacked->isChecked());
    QVERIFY(!side->isChecked());
    QTRY_VERIFY(side->isVisible() && stacked->isVisible());
    QVERIFY(side->width() > 0 && stacked->width() > 0);
    QVERIFY2(stacked->mapTo(bar, stacked->rect().bottomRight()).x() < bar->width(),
             "layout controls must remain inside the CompareBar at the saved window width");
}

QTEST_MAIN(TestComparison)
#include "tst_comparison.moc"
