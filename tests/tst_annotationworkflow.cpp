#include "annotation/AnnotationRenderer.h"
#include "annotation/AnnotationTypes.h"
#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/AnnotationToolbar.h"
#include "ui/MainWindow.h"
#include "ui/TimelineWidget.h"
#include "ui/ViewerWidget.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>

using atk::playback::PlayerState;

namespace {
QString fixturePath() { return QStringLiteral(ATK_TEST_MEDIA_DIR "/atk_sync_10s.mkv"); }

QString writeProject(QTemporaryDir& directory)
{
    atk::project::Project project;
    project.setName(QStringLiteral("Annotation workflow"));
    project.addSource(std::make_shared<atk::media::MediaSource>(fixturePath()));
    project.setActiveIndex(0);
    const QString path = directory.filePath(QStringLiteral("draw.atkproj"));
    return atk::project::ProjectSerializer::save(project, path).ok ? path : QString();
}

QAction* command(atk::ui::MainWindow& window, const char* key)
{
    return window.findChild<QAction*>(QString::fromLatin1(key));
}

void drawStroke(atk::ui::ViewerWidget* viewer, QPointF from, QPointF to)
{
    const QPoint a = viewer->normalizedToWidget(from).toPoint();
    QTest::mousePress(viewer, Qt::LeftButton, {}, a);
    for (int i = 1; i <= 6; ++i) {
        const QPointF n = from + (to - from) * (i / 6.0);
        const QPoint p = viewer->normalizedToWidget(n).toPoint();
        QMouseEvent move(QEvent::MouseMove, p, viewer->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewer, &move);
    }
    QTest::mouseRelease(viewer, Qt::LeftButton, {}, viewer->normalizedToWidget(to).toPoint());
}

bool openAt(atk::ui::MainWindow& window, const QString& project, qint64 frame)
{
    if (!window.openProjectFile(project)) return false;
    if (!QTest::qWaitFor([&] { return window.playbackController()->state() == PlayerState::Ready; }, 10000))
        return false;
    window.playbackController()->seekFrame(frame);
    return QTest::qWaitFor([&] {
        auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
        return window.playbackController()->currentFrame() == frame && viewer
            && viewer->state() == atk::ui::ViewerWidget::State::Loaded;
    }, 5000);
}
} // namespace

class TestAnnotationWorkflow : public QObject {
    Q_OBJECT
private slots:
    void drawModeNeedsMedia();
    void drawUndoRedoNavigateAndPersist();
    void brushSizeKeysStepTheToolbar();
    void typeEditMoveAndDeleteText();
};

void TestAnnotationWorkflow::drawModeNeedsMedia()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    QAction* draw = command(window, "draw.annotateMode");
    QVERIFY(draw);
    draw->trigger();
    QVERIFY(!draw->isChecked());
    QVERIFY(!window.findChild<atk::ui::ViewerWidget*>()->annotateMode());
}

void TestAnnotationWorkflow::drawUndoRedoNavigateAndPersist()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    QVERIFY(!projectPath.isEmpty());
    QString savedPath;
    {
        atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(openAt(window, projectPath, 30));
        auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
        auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();
        QVERIFY(viewer && toolbar);
        QVERIFY(!toolbar->isVisible());

        command(window, "draw.annotateMode")->trigger();
        QVERIFY(viewer->annotateMode());
        QVERIFY(toolbar->isVisible());
        QVERIFY(!command(window, "draw.undo")->isEnabled());

        drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
        const auto& entry = window.project()->entries().at(0);
        QCOMPARE(entry.annotations.annotatedFrames(), QVector<int64_t>({30}));
        QCOMPARE(viewer->annotationFrame().items.size(), 1);
        QVERIFY(window.project()->isModified());
        auto* timeline = window.findChild<atk::ui::TimelineWidget*>();
        QVERIFY(timeline);
        QCOMPARE(timeline->annotatedFrames(), QVector<int64_t>({30}));
        QVERIFY(command(window, "draw.undo")->isEnabled());

        command(window, "draw.undo")->trigger();
        QVERIFY(window.project()->entries().at(0).annotations.isEmpty());
        QVERIFY(viewer->annotationFrame().isEmpty());
        QVERIFY(timeline->annotatedFrames().isEmpty());
        command(window, "draw.redo")->trigger();
        QCOMPARE(window.project()->entries().at(0).annotations.annotatedFrames(), QVector<int64_t>({30}));

        // Another frame shows nothing; Previous Drawn Frame comes back.
        window.playbackController()->seekFrame(40);
        QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(40), 5000);
        QTRY_VERIFY(viewer->annotationFrame().isEmpty());
        command(window, "draw.previousFrame")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(30), 5000);
        QTRY_COMPARE(viewer->annotationFrame().items.size(), 1);

        // The eraser cuts the line where it crosses (area mode, the default);
        // the whole swipe is one undo step.
        auto tool = toolbar->settings();
        tool.tool = atk::annotation::AnnotationTool::Eraser;
        toolbar->setSettings(tool);
        viewer->setAnnotationTool(tool);
        drawStroke(viewer, {0.45, 0.2}, {0.45, 0.8});
        QCOMPARE(window.project()->entries().at(0).annotations.frame(30).items.size(), 2);
        QCOMPARE(viewer->annotationFrame().items.size(), 2);
        command(window, "draw.undo")->trigger();
        QCOMPARE(window.project()->entries().at(0).annotations.frame(30).items.size(), 1);

        savedPath = directory.filePath(QStringLiteral("saved.atkproj"));
        QVERIFY(atk::project::ProjectSerializer::save(*window.project(), savedPath).ok);
    }

    // Reopen: the drawing comes back on its frame.
    atk::ui::MainWindow reopened(directory.filePath(QStringLiteral("settings2.ini")));
    reopened.resize(1200, 800);
    reopened.show();
    QVERIFY(QTest::qWaitForWindowExposed(&reopened));
    QVERIFY(openAt(reopened, savedPath, 30));
    auto* viewer = reopened.findChild<atk::ui::ViewerWidget*>();
    QTRY_COMPARE(viewer->annotationFrame().items.size(), 1);
    QCOMPARE(viewer->annotationFrame().frame, qint64(30));
    QCOMPARE(reopened.findChild<atk::ui::TimelineWidget*>()->annotatedFrames(), QVector<int64_t>({30}));
    // Loading is not an edit: nothing to undo, project not dirty.
    QVERIFY(!command(reopened, "draw.undo")->isEnabled());
    QVERIFY(!reopened.project()->isModified());
}

void TestAnnotationWorkflow::brushSizeKeysStepTheToolbar()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    const qreal before = toolbar->settings().sizePixels;
    command(window, "draw.brushSizeUp")->trigger();
    QVERIFY(toolbar->settings().sizePixels > before);
    QCOMPARE(viewer->annotationTool().sizePixels, toolbar->settings().sizePixels);
    command(window, "draw.brushSizeDown")->trigger();
    command(window, "draw.brushSizeDown")->trigger();
    QVERIFY(toolbar->settings().sizePixels < before);

    // The choice is remembered for the next session.
    const qreal remembered = toolbar->settings().sizePixels;
    atk::ui::MainWindow next(directory.filePath(QStringLiteral("settings.ini")));
    QCOMPARE(next.findChild<atk::ui::AnnotationToolbar*>()->settings().sizePixels, remembered);
}

namespace {
void chooseTool(atk::ui::AnnotationToolbar* toolbar, const QString& label)
{
    for (QAction* action : toolbar->actions())
        if (action->text() == label) {
            action->trigger();
            return;
        }
    QFAIL(qPrintable(QStringLiteral("no toolbar action %1").arg(label)));
}
} // namespace

void TestAnnotationWorkflow::typeEditMoveAndDeleteText()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 30));
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();
    command(window, "draw.annotateMode")->trigger();
    auto frameItems = [&] { return window.project()->entries().at(0).annotations.frame(30).items; };

    // Type in place.
    chooseTool(toolbar, QStringLiteral("Text"));
    QCOMPARE(viewer->annotationTool().tool, atk::annotation::AnnotationTool::Text);
    QTest::mouseClick(viewer, Qt::LeftButton, {}, viewer->normalizedToWidget({0.3, 0.3}).toPoint());
    QVERIFY(viewer->isEditingText());
    QTest::keyClicks(viewer->textEditor(), QStringLiteral("fix the nose"));
    QTest::keyClick(viewer->textEditor(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(frameItems().size(), 1);
    QCOMPARE(frameItems()[0].text, QStringLiteral("fix the nose"));
    const auto placed = frameItems()[0];

    // Click it again with the Text tool to edit it.
    const QRectF target = viewer->transform().imageRect();
    const QPoint onText = atk::annotation::AnnotationRenderer::itemBounds(target, placed).center().toPoint();
    QTest::mouseClick(viewer, Qt::LeftButton, {}, onText);
    QVERIFY(viewer->isEditingText());
    viewer->textEditor()->setPlainText(QStringLiteral("fix the nose and chin"));
    QTest::keyClick(viewer->textEditor(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(frameItems()[0].text, QStringLiteral("fix the nose and chin"));
    QCOMPARE(frameItems()[0].id, placed.id);

    // Select and drag it somewhere else; undo puts it back.
    chooseTool(toolbar, QStringLiteral("Select"));
    const QPoint from = atk::annotation::AnnotationRenderer::itemBounds(target, frameItems()[0]).center().toPoint();
    const QPoint to = from + QPoint(120, 80);
    QTest::mousePress(viewer, Qt::LeftButton, {}, from);
    QMouseEvent move(QEvent::MouseMove, to, viewer->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &move);
    QTest::mouseRelease(viewer, Qt::LeftButton, {}, to);
    QVERIFY(frameItems()[0].points[0].x > placed.points[0].x + 0.05f);
    QVERIFY(frameItems()[0].points[0].y > placed.points[0].y + 0.05f);
    QCOMPARE(viewer->selectedItemId(), placed.id);
    command(window, "draw.undo")->trigger();
    QCOMPARE(frameItems()[0].points, placed.points);

    // Delete removes it; undo restores it.
    QTest::mouseClick(viewer, Qt::LeftButton, {}, onText);
    QCOMPARE(viewer->selectedItemId(), placed.id);
    QTest::keyClick(viewer, Qt::Key_Delete);
    QVERIFY(frameItems().isEmpty());
    command(window, "draw.undo")->trigger();
    QCOMPARE(frameItems().size(), 1);
}

QTEST_MAIN(TestAnnotationWorkflow)
#include "tst_annotationworkflow.moc"
