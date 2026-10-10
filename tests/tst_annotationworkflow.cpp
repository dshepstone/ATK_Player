#include "annotation/AnnotationRenderer.h"
#include "annotation/AnnotationTypes.h"
#include "annotation/BrushLibrary.h"
#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/AnnotationToolbar.h"
#include "ui/ExportDialog.h"
#include "ui/MainWindow.h"
#include "ui/NotesPanel.h"
#include "ui/ReviewNotesDialog.h"
#include "ui/TimelineWidget.h"
#include "ui/ViewerWidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QListWidget>
#include <QPlainTextEdit>
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
    // Finished exports would open in the system viewer; record them instead.
    void initTestCase() { qputenv("ATK_NO_OPEN_EXPORTS", "1"); }
    void drawModeNeedsMedia();
    void drawUndoRedoNavigateAndPersist();
    void brushSizeKeysStepTheToolbar();
    void typeEditMoveAndDeleteText();
    void importedBrushesAppearInTheToolbar();
    void notesPanelWritesNotesWhileDrawing();
    void notesPanelExportsAReviewNotesPdf();
    void notesComposerBelongsToTheFrame();
    void tracingPaperAndGhostingToggleAndPersist();
    void drawButtonAndCtrlAngleBracketsJumpBetweenDrawings();
    void fileMenuHasTheNotesExports();
    void exportsOpenOnlyOnceTheFileIsReady();
    void watchingASavedProjectNeverAsksToSave();
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

void TestAnnotationWorkflow::importedBrushesAppearInTheToolbar()
{
    QTemporaryDir directory;
    // A library that a previous session imported, beside this test's settings.
    atk::annotation::BrushSet set;
    set.name = QStringLiteral("Inks");
    atk::annotation::BrushTip tip;
    tip.id = QStringLiteral("abr-workflow-tip");
    tip.name = QStringLiteral("Wet Ink");
    tip.mask = QImage(16, 16, QImage::Format_Grayscale8);
    tip.mask.fill(255);
    set.tips = {tip};
    atk::annotation::BrushLibrary library(directory.filePath(QStringLiteral("brushes")));
    QVERIFY(library.addSet(set));

    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    QAction* pick = nullptr;
    for (QAction* action : toolbar->findChildren<QAction*>())
        if (action->text() == QStringLiteral("Wet Ink")) pick = action;
    QVERIFY2(pick, "the imported brush is listed in the Brushes menu");
    pick->trigger();
    QCOMPARE(viewer->annotationTool().tool, atk::annotation::AnnotationTool::Stamp);
    QCOMPARE(viewer->annotationTool().brushTipId, tip.id);
    QVERIFY(atk::annotation::BrushTipRegistry::instance().contains(tip.id));

    // The choice is remembered.
    atk::ui::MainWindow next(directory.filePath(QStringLiteral("settings.ini")));
    QCOMPARE(next.findChild<atk::ui::ViewerWidget*>()->annotationTool().brushTipId, tip.id);
}

void TestAnnotationWorkflow::notesPanelWritesNotesWhileDrawing()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* panel = window.findChild<atk::ui::NotesPanel*>();
    QVERIFY(panel && panel->isVisible()); // on the right by default
    QVERIFY(!panel->composer()->isEnabled()); // no clip yet
    QVERIFY(openAt(window, projectPath, 30));
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    QTRY_COMPARE(panel->currentFrame(), qint64(30));
    QVERIFY(panel->composer()->isEnabled());

    // Draw on the frame and write its note, SyncSketch style.
    command(window, "draw.annotateMode")->trigger();
    drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
    panel->setReviewerName(QStringLiteral("David"));
    panel->composer()->setFocus();
    QTest::keyClicks(panel->composer(), QStringLiteral("fix the nose"));
    QTest::keyClick(panel->composer(), Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(panel->composer()->toPlainText().isEmpty());
    const auto frame30 = window.project()->entries().at(0).annotations.frame(30);
    QCOMPARE(frame30.items.size(), 1);
    QCOMPARE(frame30.notes.size(), 1);
    QCOMPARE(frame30.notes[0].text, QStringLiteral("fix the nose"));
    QCOMPARE(frame30.notes[0].author, QStringLiteral("David"));
    QVERIFY(frame30.notes[0].createdMsUtc > 0);
    QCOMPARE(panel->notes().size(), 1);
    QVERIFY(panel->notes()[0].frameHasDrawing);
    QCOMPARE(panel->list()->count(), 1);
    QVERIFY(panel->list()->item(0)->text().contains(QStringLiteral("fix the nose")));

    // A note on another frame; clicking a note jumps to its frame.
    window.playbackController()->seekFrame(45);
    QTRY_COMPARE(panel->currentFrame(), qint64(45));
    QTest::keyClicks(panel->composer(), QStringLiteral("timing"));
    QTest::keyClick(panel->composer(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(panel->list()->count(), 2);
    QListWidgetItem* first = panel->list()->item(0);
    QTest::mouseClick(panel->list()->viewport(), Qt::LeftButton, {},
                      panel->list()->visualItemRect(first).center());
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(30), 5000);

    // Notes undo like drawings, and save with the project.
    command(window, "draw.undo")->trigger();
    QCOMPARE(panel->notes().size(), 1);
    command(window, "draw.redo")->trigger();
    QCOMPARE(panel->notes().size(), 2);
    const QString saved = directory.filePath(QStringLiteral("notes.atkproj"));
    QVERIFY(atk::project::ProjectSerializer::save(*window.project(), saved).ok);
    atk::project::Project loaded;
    QVERIFY(atk::project::ProjectSerializer::load(loaded, saved).ok);
    QCOMPARE(loaded.entries().at(0).annotations.frame(45).notes.size(), 1);

    // F6 hides and shows the panel.
    command(window, "view.toggleNotes")->trigger();
    QVERIFY(!panel->isVisible());
    command(window, "view.toggleNotes")->trigger();
    QVERIFY(panel->isVisible());
}

void TestAnnotationWorkflow::notesPanelExportsAReviewNotesPdf()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* panel = window.findChild<atk::ui::NotesPanel*>();
    auto* exportPdf = panel->findChild<QPushButton*>(QStringLiteral("NotesExportPdf"));
    QVERIFY(exportPdf && !exportPdf->isEnabled()); // nothing to export without a clip
    QVERIFY(openAt(window, projectPath, 30));
    QTRY_VERIFY(exportPdf->isEnabled());
    command(window, "draw.annotateMode")->trigger();
    drawStroke(window.findChild<atk::ui::ViewerWidget*>(), {0.3, 0.4}, {0.6, 0.5});
    QTest::keyClicks(panel->composer(), QStringLiteral("hold this pose"));
    QTest::keyClick(panel->composer(), Qt::Key_Return, Qt::ControlModifier);

    // Click "Export PDF…" and accept the dialog; the finished PDF opens.
    const QString pdf = directory.filePath(QStringLiteral("review_notes.pdf"));
    bool sawDialog = false;
    bool openOffered = false;
    bool tracingOffered = false;
    QString summary;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&] {
        QWidget* modal = QApplication::activeModalWidget();
        if (auto* dialog = qobject_cast<atk::ui::ReviewNotesDialog*>(modal)) {
            sawDialog = true;
            summary = dialog->findChild<QLabel*>(QStringLiteral("ReviewNotesSummary"))->text();
            openOffered = dialog->openWhenDone();
            tracingOffered = dialog->findChild<QComboBox*>(QStringLiteral("ReviewNotesTracingPaper")) != nullptr;
            dialog->findChild<QLineEdit*>(QStringLiteral("ReviewNotesDestination"))->setText(pdf);
            dialog->findChild<QPushButton*>(QStringLiteral("ReviewNotesExport"))->click();
        }
    });
    driver.start();
    exportPdf->click();
    QTRY_COMPARE_WITH_TIMEOUT(QFileInfo(window.lastOpenedExport()).absoluteFilePath(), QFileInfo(pdf).absoluteFilePath(), 20000);
    driver.stop();
    QVERIFY(sawDialog);
    QVERIFY(openOffered);   // on by default
    QVERIFY(tracingOffered);
    QCOMPARE(summary, QStringLiteral("1 frame(s) will be listed."));
    QFile file(pdf);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(4) == "%PDF");
    QVERIFY(!QFile::exists(pdf + QStringLiteral(".partial")));
}

void TestAnnotationWorkflow::notesComposerBelongsToTheFrame()
{
    atk::ui::NotesPanel panel;
    panel.resize(320, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QSignalSpy added(&panel, &atk::ui::NotesPanel::addNoteRequested);
    panel.setCurrentFrame(15, QStringLiteral("00:00:00:15"), true);
    panel.composer()->setFocus();

    // Shift+Enter is a new line; Enter submits to the frame on screen.
    QTest::keyClicks(panel.composer(), QStringLiteral("arc"));
    QTest::keyClick(panel.composer(), Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(panel.composer(), QStringLiteral("smoother"));
    QCOMPARE(panel.composer()->toPlainText(), QStringLiteral("arc\nsmoother"));
    QTest::keyClick(panel.composer(), Qt::Key_Return);
    QCOMPARE(added.size(), 1);
    QCOMPARE(added[0][0].toLongLong(), qint64(15));
    QCOMPARE(added[0][1].toString(), QStringLiteral("arc\nsmoother"));
    QVERIFY(panel.composer()->toPlainText().isEmpty());

    // An unsent draft stays with its frame: another frame starts empty.
    QTest::keyClicks(panel.composer(), QStringLiteral("unfinished"));
    panel.setCurrentFrame(24, QStringLiteral("00:00:01:00"), true);
    QVERIFY(panel.composer()->toPlainText().isEmpty());
    QTest::keyClicks(panel.composer(), QStringLiteral("frame 25 note"));
    QTest::keyClick(panel.composer(), Qt::Key_Return);
    QCOMPARE(added.size(), 2);
    QCOMPARE(added[1][0].toLongLong(), qint64(24));
    panel.setCurrentFrame(15, QStringLiteral("00:00:00:15"), true);
    QCOMPARE(panel.composer()->toPlainText(), QStringLiteral("unfinished"));
    panel.setCurrentFrame(24, QStringLiteral("00:00:01:00"), true);
    QVERIFY(panel.composer()->toPlainText().isEmpty()); // submitted, so no draft

    // The list: all notes by default, or only the frame on screen.
    atk::annotation::FrameNote a{1, QStringLiteral("arc"), QStringLiteral("David"), 1, false};
    atk::annotation::FrameNote b{2, QStringLiteral("frame 25 note"), QStringLiteral("David"), 2, true};
    panel.setNotes({{15, QStringLiteral("00:00:00:15"), a, true}, {24, QStringLiteral("00:00:01:00"), b, false}});
    QCOMPARE(panel.list()->count(), 2);
    panel.setFilter(atk::ui::NotesPanel::Filter::ThisFrame);
    QCOMPARE(panel.list()->count(), 1);
    QCOMPARE(panel.list()->item(0)->text(), QStringLiteral("frame 25 note"));
    panel.setCurrentFrame(40, QStringLiteral("00:00:01:16"), true);
    QCOMPARE(panel.list()->count(), 0); // nothing on frame 41 yet
    panel.setFilter(atk::ui::NotesPanel::Filter::Open);
    QCOMPARE(panel.list()->count(), 1);
    panel.setFilter(atk::ui::NotesPanel::Filter::Resolved);
    QCOMPARE(panel.list()->count(), 1);
    QCOMPARE(panel.list()->item(0)->text(), QStringLiteral("frame 25 note"));
}

void TestAnnotationWorkflow::tracingPaperAndGhostingToggleAndPersist()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    const QString settings = directory.filePath(QStringLiteral("settings.ini"));
    {
        atk::ui::MainWindow window(settings);
        window.resize(1400, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(openAt(window, projectPath, 20));
        auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
        auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();
        QCOMPARE(viewer->tracingPaper(), 0);

        // T toggles tracing paper at the default 50%; the arrow menu picks a level.
        command(window, "draw.tracingPaper")->trigger();
        QCOMPARE(viewer->tracingPaper(), 50);
        QVERIFY(toolbar->tracingPaperButton()->isChecked());
        emit toolbar->tracingPaperRequested(75);
        QCOMPARE(viewer->tracingPaper(), 75);
        QVERIFY(command(window, "draw.tracingPaper")->isChecked());
        toolbar->tracingPaperButton()->click(); // off
        QCOMPARE(viewer->tracingPaper(), 0);
        QVERIFY(!command(window, "draw.tracingPaper")->isChecked());
        command(window, "draw.tracingPaper")->trigger(); // back on at the last level
        QCOMPARE(viewer->tracingPaper(), 75);

        // Draw on frames 18 and 22, then ghost them from frame 20.
        command(window, "draw.annotateMode")->trigger();
        for (qint64 f : {18, 22}) {
            window.playbackController()->seekFrame(f);
            QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), f, 5000);
            QTRY_VERIFY(viewer->state() == atk::ui::ViewerWidget::State::Loaded);
            drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
        }
        window.playbackController()->seekFrame(20);
        QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(20), 5000);
        QVERIFY(viewer->ghostFrames().isEmpty());
        command(window, "draw.ghosting")->trigger(); // G, default 3 back / 3 forward
        QTRY_COMPARE(viewer->ghostFrames().size(), 2);
        QVERIFY(toolbar->ghostButton()->isChecked());
        auto* before = toolbar->findChild<QSpinBox*>(QStringLiteral("GhostBefore"));
        QVERIFY(before);
        before->setValue(1); // frame 18 is now out of reach
        QCOMPARE(viewer->ghostFrames().size(), 1);
        QCOMPARE(viewer->ghostFrames()[0].offset, 2);
        // Moving to another frame re-centres the ghosts.
        window.playbackController()->seekFrame(19);
        QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(19), 5000);
        QTRY_COMPARE(viewer->ghostFrames().size(), 2); // 18 is 1 back, 22 is 3 forward
        QCOMPARE(viewer->ghostFrames()[0].offset, 3);
        QCOMPARE(viewer->ghostFrames()[1].offset, -1);
    }
    // Both survive a restart.
    atk::ui::MainWindow again(settings);
    auto* viewer = again.findChild<atk::ui::ViewerWidget*>();
    QCOMPARE(viewer->tracingPaper(), 75);
    QVERIFY(command(again, "draw.ghosting")->isChecked());
    QCOMPARE(again.findChild<QSpinBox*>(QStringLiteral("GhostBefore"))->value(), 1);
    QCOMPARE(again.findChild<QSpinBox*>(QStringLiteral("GhostAfter"))->value(), 3);
}

void TestAnnotationWorkflow::drawButtonAndCtrlAngleBracketsJumpBetweenDrawings()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 10));
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();

    // The Draw button on the transport row opens the drawing tools.
    auto* drawButton = window.findChild<QToolButton*>(QStringLiteral("TransportDrawButton"));
    QVERIFY(drawButton && drawButton->isVisible() && drawButton->isEnabled());
    QVERIFY(!toolbar->isVisible());
    drawButton->click();
    QVERIFY(viewer->annotateMode());
    QVERIFY(toolbar->isVisible());
    QVERIFY(drawButton->isChecked());
    drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
    window.playbackController()->seekFrame(30);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(30), 5000);
    QTRY_VERIFY(viewer->state() == atk::ui::ViewerWidget::State::Loaded);
    drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
    drawButton->click();
    QVERIFY(!viewer->annotateMode());
    QVERIFY(!toolbar->isVisible());

    // Ctrl + > / < (the . and , keys, shifted or not) step between drawings.
    window.playbackController()->seekFrame(0);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(0), 5000);
    viewer->setFocus();
    QTest::keyClick(viewer, Qt::Key_Period, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(10), 5000);
    QTest::keyClick(viewer, Qt::Key_Greater, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(30), 5000);
    QTest::keyClick(viewer, Qt::Key_Comma, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(10), 5000);
    QTest::keyClick(viewer, Qt::Key_Less, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(30), 5000); // wraps
    // Also from the notes composer, so you can jump while writing notes.
    auto* panel = window.findChild<atk::ui::NotesPanel*>();
    panel->composer()->setFocus();
    QTest::keyClick(panel->composer(), Qt::Key_Period, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(10), 5000);
    QCOMPARE(command(window, "draw.nextFrame")->shortcut(), QKeySequence(QStringLiteral("Ctrl+.")));
    QCOMPARE(command(window, "draw.previousFrame")->shortcut(), QKeySequence(QStringLiteral("Ctrl+,")));
    QCOMPARE(command(window, "edit.preferences")->shortcut(), QKeySequence(QStringLiteral("Ctrl+K")));
}

void TestAnnotationWorkflow::fileMenuHasTheNotesExports()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QAction* pdf = command(window, "file.exportReviewNotes");
    QAction* video = command(window, "file.exportNotesVideo");
    QVERIFY(pdf && video);

    // Both sit in the File menu, together, after the other exports.
    QMenu* fileMenu = nullptr;
    for (QAction* top : window.menuBar()->actions())
        if (top->menu() && top->menu()->actions().contains(command(window, "file.exportReview"))) fileMenu = top->menu();
    QVERIFY(fileMenu);
    const QList<QAction*> items = fileMenu->actions();
    const int pdfAt = int(items.indexOf(pdf));
    QVERIFY(pdfAt > int(items.indexOf(command(window, "file.exportImageSequence"))));
    QCOMPARE(items.value(pdfAt + 1), video);
    QVERIFY(items.value(pdfAt - 1)->isSeparator());
    QCOMPARE(pdf->text(), QStringLiteral("Export Notes PDF..."));
    QCOMPARE(video->text(), QStringLiteral("Export Notes Video (MP4)..."));

    // Export Notes Video opens Export Review with drawings and notes on.
    QVERIFY(openAt(window, projectPath, 5));
    QVERIFY(video->isEnabled());
    bool checked = false;
    QString destination;
    QTimer::singleShot(0, this, [&] {
        QTRY_VERIFY(qobject_cast<atk::ui::ExportDialog*>(QApplication::activeModalWidget()));
        auto* dialog = qobject_cast<atk::ui::ExportDialog*>(QApplication::activeModalWidget());
        checked = dialog->burnIns().annotations && dialog->burnIns().annotationNotes;
        destination = dialog->destination();
        dialog->reject();
    });
    video->trigger();
    QVERIFY(checked);
    QVERIFY2(destination.endsWith(QStringLiteral("_notes.mp4")), qPrintable(destination));
}

// A sync app (Google Drive) holds a new file for a moment; opening it then
// fails. The export opens only once the file is free and readable.
void TestAnnotationWorkflow::exportsOpenOnlyOnceTheFileIsReady()
{
    QTemporaryDir directory;
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    const QString path = directory.filePath(QStringLiteral("notes.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly)); // empty: not ready yet
    file.close();
    window.openExportedFile(path);
    QTest::qWait(700);
    QVERIFY(window.lastOpenedExport().isEmpty()); // still waiting
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("%PDF-1.4 ready");
    file.close();
    QTRY_COMPARE_WITH_TIMEOUT(window.lastOpenedExport(), path, 3000);

    // A file that is ready opens straight away.
    const QString ready = directory.filePath(QStringLiteral("ready.pdf"));
    QFile second(ready);
    QVERIFY(second.open(QIODevice::WriteOnly));
    second.write("%PDF-1.4");
    second.close();
    window.openExportedFile(ready);
    QCOMPARE(window.lastOpenedExport(), ready);
}

// Opening a project or a video and just watching, scrubbing or zooming the
// timeline must not lead to a "save changes?" question.
void TestAnnotationWorkflow::watchingASavedProjectNeverAsksToSave()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 30));
    window.playbackController()->seekFrame(120);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(120), 5000);
    command(window, "view.timelineZoomIn")->trigger();
    command(window, "view.timelineZoomIn")->trigger();
    command(window, "playback.nextFrame")->trigger();
    QVERIFY(!window.project()->needsSavePrompt());

    // A drawing is review work: now it asks.
    command(window, "draw.annotateMode")->trigger();
    drawStroke(window.findChild<atk::ui::ViewerWidget*>(), {0.3, 0.4}, {0.6, 0.5});
    QVERIFY(window.project()->needsSavePrompt());
    command(window, "draw.undo")->trigger(); // and stops asking once it is gone
    QVERIFY(!window.project()->needsSavePrompt());

    // A single video opened on its own: watching it never asks either.
    const QString clip = window.project()->entries().at(0).source->filePath();
    window.openMediaFile(clip);
    QTRY_VERIFY_WITH_TIMEOUT(window.playbackController()->state() == PlayerState::Ready, 10000);
    window.playbackController()->seekFrame(60);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(60), 5000);
    command(window, "view.timelineZoomIn")->trigger();
    QVERIFY(!window.project()->needsSavePrompt());
}

QTEST_MAIN(TestAnnotationWorkflow)
#include "tst_annotationworkflow.moc"
