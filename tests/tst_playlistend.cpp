#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDir>
#include "ui/ViewerWidget.h"
#include <QDropEvent>
#include <QImage>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include "ui/ApplicationSettings.h"
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
    void droppedStillPlaysHeldExtentAndStops();
    void droppedSequenceFramesOpenOneSequenceAtPreferenceRate();
    void flipCommandMirrorsAllViewersAndResetsOnOpen();
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

void TestPlaylistEnd::droppedStillPlaysHeldExtentAndStops()
{
    QTemporaryDir directory;
    const QString settingsFile = directory.filePath(QStringLiteral("settings.ini"));
    {
        atk::ui::ApplicationSettings settings(settingsFile);
        settings.setStillImageHoldFrames(12);
        settings.sync();
    }
    atk::ui::MainWindow window(settingsFile);
    QVERIFY(window.acceptDrops());

    const QString still = QStringLiteral(ATK_TEST_MEDIA_DIR "/atk_still_320x180.png");
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(still),
                  QUrl::fromLocalFile(directory.filePath(QStringLiteral("notes.txt"))),
                  QUrl(QStringLiteral("https://example.com/clip.mp4"))});
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    QVERIFY(drop.isAccepted());

    // Only the supported local file was added, with the preference as its hold.
    QCOMPARE(window.project()->entries().size(), 1);
    const auto source = window.project()->entries().at(0).source;
    QCOMPARE(source->imageOptions().holdFrames, qint64(12));

    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    QVERIFY(window.playbackController()->metadata().isStillImage);
    QCOMPARE(window.playbackController()->metadata().frameCount, qint64(12));
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ended, 10000);
    QCOMPARE(window.playbackController()->currentFrame(), qint64(11));

    // A drag with nothing usable is refused outright.
    QMimeData unsupported;
    unsupported.setUrls({QUrl::fromLocalFile(directory.filePath(QStringLiteral("notes.txt")))});
    QDragEnterEvent refused(QPoint(10, 10), Qt::CopyAction, &unsupported, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &refused);
    QVERIFY(!refused.isAccepted());
}

void TestPlaylistEnd::droppedSequenceFramesOpenOneSequenceAtPreferenceRate()
{
    QTemporaryDir directory;
    const QString settingsFile = directory.filePath(QStringLiteral("settings.ini"));
    {
        atk::ui::ApplicationSettings settings(settingsFile);
        settings.setImageSequenceFrameRate({25, 1});
        settings.sync();
    }
    QStringList frames;
    for (int number = 1001; number <= 1012; ++number) {
        QImage image(64, 36, QImage::Format_RGB32);
        image.fill(QColor(number % 256, 0, 0));
        const QString path = QDir(directory.path()).filePath(QStringLiteral("shot.%1.png").arg(number));
        QVERIFY(image.save(path, "PNG"));
        frames << path;
    }
    atk::ui::MainWindow window(settingsFile);

    // Every frame is dropped at once; the prompt appears once and the answer
    // covers the whole sequence.
    int prompts = 0;
    QTimer answer;
    answer.setInterval(20);
    QObject::connect(&answer, &QTimer::timeout, &window, [&prompts] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            box && box->objectName() == QStringLiteral("ImageSequencePrompt")) {
            ++prompts;
            box->findChild<QPushButton*>(QStringLiteral("ImageSequencePromptSequence"))->click();
        }
    });
    answer.start();
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString& frame : frames) urls << QUrl::fromLocalFile(frame);
    mime.setUrls(urls);
    // Qt delivers a drop only to a widget that accepted the drag's entry.
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    answer.stop();
    QCOMPARE(prompts, 1);

    QCOMPARE(window.project()->entries().size(), 1);
    const auto source = window.project()->entries().at(0).source;
    QVERIFY(source->isImageSequence());
    QCOMPARE(source->displayName(), QStringLiteral("shot.[1001-1012].png"));
    QCOMPARE(source->imageOptions().frameRate, (atk::media::FrameRate{25, 1}));

    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    QVERIFY(window.playbackController()->metadata().isImageSequence);
    QCOMPARE(window.playbackController()->metadata().frameCount, qint64(12));
    window.playbackController()->seekFrame(5);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(5), 5000);

    // Re-timing keeps frame indices (and so bookmarks) and reopens the source.
    QVERIFY(window.setSourceFrameRate(0, {24000, 1001}));
    QVERIFY(window.project()->isModified());
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->metadata().frameRate,
                              (atk::media::FrameRate{24000, 1001}), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    QCOMPARE(window.playbackController()->metadata().frameCount, qint64(12));
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ended, 10000);
    QCOMPARE(window.playbackController()->currentFrame(), qint64(11));

    // Choosing "Single Image" adds just that file as a still.
    answer.disconnect();
    QObject::connect(&answer, &QTimer::timeout, &window, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->findChild<QPushButton*>(QStringLiteral("ImageSequencePromptStill"))->click();
    });
    answer.start();
    QMimeData one;
    one.setUrls({QUrl::fromLocalFile(frames.at(3))});
    QDragEnterEvent enterOne(QPoint(10, 10), Qt::CopyAction, &one, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enterOne);
    QDropEvent single(QPointF(10, 10), Qt::CopyAction, &one, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &single);
    answer.stop();
    QCOMPARE(window.project()->entries().size(), 2);
    QVERIFY(window.project()->entries().at(1).source->isStillImage());
}

void TestPlaylistEnd::flipCommandMirrorsAllViewersAndResetsOnOpen()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(writeProject(directory, 2)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);

    QAction* flip = command(window, "view.flipHorizontal");
    QVERIFY(flip && flip->isCheckable());
    flip->trigger();
    QVERIFY(window.isFlippedHorizontally());
    QVERIFY(flip->isChecked());
    const auto viewers = window.findChildren<atk::ui::ViewerWidget*>();
    QVERIFY(!viewers.isEmpty());
    for (auto* viewer : viewers) QVERIFY(viewer->isFlippedHorizontally());
    QCOMPARE(window.playbackController()->currentFrame(), qint64(0)); // no seek, no reopen
    QVERIFY(!window.project()->isModified());                        // never saved

    // A newly opened source starts unflipped.
    QVERIFY(window.isFlippedHorizontally());
    window.playbackController()->openMedia(fixturePath());
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFlippedHorizontally(), 10000);
    QVERIFY(!flip->isChecked());
    for (auto* viewer : viewers) QVERIFY(!viewer->isFlippedHorizontally());
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
