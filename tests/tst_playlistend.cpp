#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using atk::playback::PlayerState;

namespace {

QString fixturePath()
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/atk_sync_10s.mkv");
}

QString writeProject(QTemporaryDir& directory, int sourceCount, int missingIndex = -1,
                     atk::timeline::PlaybackRange firstRange = {})
{
    atk::project::Project project;
    project.setName(QStringLiteral("Playlist end test"));
    for (int i = 0; i < sourceCount; ++i) {
        const QString path = i == missingIndex
            ? directory.filePath(QStringLiteral("missing-%1.mkv").arg(i))
            : fixturePath();
        project.addSource(std::make_shared<atk::media::MediaSource>(path));
        project.mutableEntries().last().displayName = QStringLiteral("Source %1").arg(i + 1);
        if (i == 0) project.mutableEntries().last().playbackRange = firstRange;
    }
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("playlist.atkproj"));
    const auto result = atk::project::ProjectSerializer::save(project, path);
    return result.ok ? path : QString();
}

QAction* command(atk::ui::MainWindow& window, const char* key)
{
    return window.findChild<QAction*>(QString::fromLatin1(key));
}

bool sawFrame(const QSignalSpy& spy, qint64 frame)
{
    for (const auto& arguments : spy) {
        if (qvariant_cast<atk::media::VideoFrame>(arguments.at(0)).frameIndex == frame)
            return true;
    }
    return false;
}

} // namespace

class TestPlaylistEnd : public QObject {
    Q_OBJECT
private slots:
    void pausedJumpToEndIsNavigationOnly();
    void playingJumpToEndAdvancesExactlyOnce();
    void playingJumpToEndSkipsMissingSource();
    void playingJumpToReviewEndAdvances();
    void loopOnJumpToReviewEndWrapsCurrentSource();
    void finalAndSingleSourceStopAtEnd();
    void playFromExactEndAdvances();
    void pausedExactSeekDoesNotAdvance();
    void naturalPlaybackStillAdvancesAndStops();
    void aboutDialogUsesAnimationToolKitCopy();
};

void TestPlaylistEnd::pausedJumpToEndIsNavigationOnly()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 2)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    const qint64 end = window.playbackController()->metadata().effectiveFrameCount() - 1;
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), end, 5000);
    QCOMPARE(window.project()->activeIndex(), 0);
    QVERIFY(!window.playbackController()->isPlaying());
}

void TestPlaylistEnd::playingJumpToEndAdvancesExactlyOnce()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 3)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    const qint64 end = window.playbackController()->metadata().effectiveFrameCount() - 1;
    QSignalSpy frames(window.playbackController(), &atk::playback::PlaybackController::frameChanged);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 10000);
    QVERIFY(sawFrame(frames, end));
    QTest::qWait(300);
    QCOMPARE(window.project()->activeIndex(), 1);
}

void TestPlaylistEnd::playingJumpToEndSkipsMissingSource()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 3, 1)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    window.playbackController()->play();
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 10000);
}

void TestPlaylistEnd::playingJumpToReviewEndAdvances()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 2, -1, {100, 150, true})));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    QSignalSpy frames(window.playbackController(), &atk::playback::PlaybackController::frameChanged);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    frames.clear();
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 1, 10000);
    QVERIFY(sawFrame(frames, 150));
}

void TestPlaylistEnd::loopOnJumpToReviewEndWrapsCurrentSource()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 2, -1, {100, 150, true})));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    window.playbackController()->setLoopEnabled(true);
    QSignalSpy frames(window.playbackController(), &atk::playback::PlaybackController::frameChanged);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    frames.clear();
    command(window, "playback.lastFrame")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(sawFrame(frames, 150), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(sawFrame(frames, 100), 5000);
    QCOMPARE(window.project()->activeIndex(), 0);
    QCOMPARE(window.playbackController()->state(), PlayerState::Playing);
}

void TestPlaylistEnd::finalAndSingleSourceStopAtEnd()
{
    for (const int count : {1, 2}) {
        QTemporaryDir directory;
        atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
        QVERIFY(window.openProjectFile(writeProject(directory, count)));
        QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
        if (count == 2) {
            command(window, "playlist.next")->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 1, 10000);
            QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
        }
        const qint64 end = window.playbackController()->metadata().effectiveFrameCount() - 1;
        window.playbackController()->play();
        command(window, "playback.lastFrame")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ended, 10000);
        QCOMPARE(window.playbackController()->currentFrame(), end);
        QCOMPARE(window.project()->activeIndex(), count - 1);
    }
}

void TestPlaylistEnd::playFromExactEndAdvances()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 2)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(),
                              window.playbackController()->metadata().effectiveFrameCount() - 1, 5000);
    command(window, "playback.playPause")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 10000);
}

void TestPlaylistEnd::pausedExactSeekDoesNotAdvance()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 2)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    const qint64 end = window.playbackController()->metadata().effectiveFrameCount() - 1;
    window.playbackController()->seekFrame(end);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), end, 5000);
    QCOMPARE(window.project()->activeIndex(), 0);
}

void TestPlaylistEnd::naturalPlaybackStillAdvancesAndStops()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 3, -1, {0, 2, true})));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 10000);
    window.playbackController()->activateReviewRange(0, 2);
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 2, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 10000);
    window.playbackController()->activateReviewRange(0, 2);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ended, 10000);
    QCOMPARE(window.project()->activeIndex(), 2);
}

void TestPlaylistEnd::aboutDialogUsesAnimationToolKitCopy()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QString text;
    QTimer::singleShot(0, [&text] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        text = dialog->text();
        dialog->accept();
    });
    command(window, "help.about")->trigger();
    QVERIFY(text.contains(QStringLiteral("Animation Tool Kit - Media Player")));
    QVERIFY(text.contains(QStringLiteral("Animation Tool Kit - Maya tools series")));
    QVERIFY(text.contains(QStringLiteral("Created By David Shepstone")));
    QVERIFY(text.contains(QStringLiteral(ATK_VERSION_STRING)));
}

QTEST_MAIN(TestPlaylistEnd)
#include "tst_playlistend.moc"
