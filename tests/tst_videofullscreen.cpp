#include "core/commands/CommandDefinitions.h"
#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/ApplicationSettings.h"
#include "ui/MainWindow.h"
#include "ui/VideoFullscreenWindow.h"
#include "ui/ViewerWidget.h"

#include <QAction>
#include <QTemporaryDir>
#include <QTest>

using atk::commands::CommandId;
using atk::playback::PlayerState;

namespace {

QString fixturePath()
{
    return QStringLiteral(ATK_TEST_MEDIA_DIR "/atk_sync_10s.mkv");
}

QString writeProject(QTemporaryDir& directory, int count)
{
    atk::project::Project project;
    project.setName(QStringLiteral("Video fullscreen test"));
    for (int i = 0; i < count; ++i) {
        project.addSource(std::make_shared<atk::media::MediaSource>(fixturePath()));
        project.mutableEntries().last().displayName = QStringLiteral("Source %1").arg(i + 1);
    }
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("fullscreen.atkproj"));
    return atk::project::ProjectSerializer::save(project, path).ok ? path : QString();
}

QAction* command(atk::ui::MainWindow& window, const char* key)
{
    return window.findChild<QAction*>(QString::fromLatin1(key));
}

} // namespace

class TestVideoFullscreen : public QObject {
    Q_OBJECT

private slots:
    void commandDefinitionAndNoMediaState();
    void enterExitPreservesPausedStateFrameSourceAndTransform();
    void shortcutAndEscapeSynchronizeAction();
    void playingPlaylistTransitionRemainsFullscreen();
    void loopAndLayoutRemainUnchanged();
    void shutdownWhileFullscreenIsSafe();
};

void TestVideoFullscreen::commandDefinitionAndNoMediaState()
{
    const auto* application = atk::commands::find(CommandId::ToggleFullScreen);
    const auto* video = atk::commands::find(CommandId::ToggleVideoFullScreen);
    QVERIFY(application && video);
    QCOMPARE(QString::fromLatin1(application->defaultShortcut), QStringLiteral("F11"));
    QCOMPARE(QString::fromLatin1(video->key), QStringLiteral("view.toggleVideoFullScreen"));
    QCOMPARE(QString::fromLatin1(video->defaultShortcut), QStringLiteral("Ctrl+Shift+F"));
    QCOMPARE(QString::fromLatin1(atk::commands::find(CommandId::TimelineZoomFit)->defaultShortcut),
             QStringLiteral("F"));
    QCOMPARE(QString::fromLatin1(atk::commands::find(CommandId::ZoomFit)->defaultShortcut),
             QStringLiteral("Ctrl+0"));
    QCOMPARE(QString::fromLatin1(atk::commands::find(CommandId::ZoomActualSize)->defaultShortcut),
             QStringLiteral("Ctrl+1"));

    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QAction* action = command(window, "view.toggleVideoFullScreen");
    QVERIFY(action);
    QVERIFY(!action->isEnabled());
    action->trigger();
    QVERIFY(!window.isVideoFullScreen());
    QVERIFY(!action->isChecked());
}

void TestVideoFullscreen::enterExitPreservesPausedStateFrameSourceAndTransform()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory, 2)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    window.playbackController()->seekFrame(50);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(50), 5000);
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    viewer->showActualSize();
    viewer->zoomIn();
    const auto before = viewer->transform();
    const int source = window.project()->activeIndex();
    const PlayerState state = window.playbackController()->state();

    window.enterVideoFullScreen();
    QTRY_VERIFY(window.isVideoFullScreen());
    QVERIFY(viewer->transform().isFit());
    QCOMPARE(window.playbackController()->state(), state);
    QCOMPARE(window.playbackController()->currentFrame(), qint64(50));
    QCOMPARE(window.project()->activeIndex(), source);

    window.exitVideoFullScreen();
    QVERIFY(!window.isVideoFullScreen());
    QCOMPARE(window.playbackController()->state(), state);
    QCOMPARE(window.playbackController()->currentFrame(), qint64(50));
    QCOMPARE(window.project()->activeIndex(), source);
    QCOMPARE(viewer->transform().mode(), before.mode());
    QCOMPARE(viewer->transform().scale(), before.scale());
    QCOMPARE(viewer->transform().pan(), before.pan());
}

void TestVideoFullscreen::shortcutAndEscapeSynchronizeAction()
{
    QTemporaryDir directory;
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    {
        atk::ui::ApplicationSettings settings(settingsPath);
        QVERIFY(settings.setShortcutOverride(QStringLiteral("view.toggleVideoFullScreen"),
                                             QStringLiteral("Ctrl+Alt+V")));
        settings.sync();
    }
    atk::ui::MainWindow window(settingsPath);
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory, 1)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    QAction* action = command(window, "view.toggleVideoFullScreen");
    QVERIFY(action->isEnabled());
    QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+V")));
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+Alt+V")));
    QTRY_VERIFY(window.isVideoFullScreen());
    QVERIFY(action->isChecked());
    auto* fullscreen = window.findChild<atk::ui::VideoFullscreenWindow*>();
    QVERIFY(fullscreen);
    QTest::keyClick(fullscreen, Qt::Key_Escape);
    QTRY_VERIFY(!window.isVideoFullScreen());
    QVERIFY(!action->isChecked());
}

void TestVideoFullscreen::playingPlaylistTransitionRemainsFullscreen()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory, 2)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    window.playbackController()->play();
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 5000);
    window.enterVideoFullScreen();
    command(window, "playback.lastFrame")->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(window.project()->activeIndex(), 1, 10000);
    QTRY_VERIFY(window.isVideoFullScreen());
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Playing, 10000);
}

void TestVideoFullscreen::loopAndLayoutRemainUnchanged()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.show();
    QVERIFY(window.openProjectFile(writeProject(directory, 1)));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), PlayerState::Ready, 10000);
    const QByteArray geometry = window.saveGeometry();
    const QByteArray layout = window.saveState();
    const bool modified = window.project()->isModified();
    window.playbackController()->setLoopEnabled(true);
    window.enterVideoFullScreen();
    QVERIFY(window.isVideoFullScreen());
    QVERIFY(window.playbackController()->isLoopEnabled());
    window.exitVideoFullScreen();
    QCOMPARE(window.saveGeometry(), geometry);
    QCOMPARE(window.saveState(), layout);
    QCOMPARE(window.project()->isModified(), modified);
}

void TestVideoFullscreen::shutdownWhileFullscreenIsSafe()
{
    QTemporaryDir directory;
    auto* window = new atk::ui::MainWindow(directory.filePath(QStringLiteral("settings.ini")));
    window->show();
    QVERIFY(window->openProjectFile(writeProject(directory, 1)));
    QTRY_COMPARE_WITH_TIMEOUT(window->playbackController()->state(), PlayerState::Ready, 10000);
    window->enterVideoFullScreen();
    QVERIFY(window->isVideoFullScreen());
    delete window;
}

QTEST_MAIN(TestVideoFullscreen)
#include "tst_videofullscreen.moc"
