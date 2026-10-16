#include "annotation/AnnotationRenderer.h"
#include "annotation/AnnotationTypes.h"
#include "annotation/BrushLibrary.h"
#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/BatchNotesPdfDialog.h"
#include "export/ReviewNotesPdf.h"
#include "ui/AnnotationToolbar.h"
#include "ui/BrushSizePill.h"
#include "ui/ExportDialog.h"
#include "ui/ExportUi.h"
#include "ui/MainWindow.h"
#include "ui/NotesPanel.h"
#include "ui/ReviewNotesDialog.h"
#include "ui/TimelineWidget.h"
#include "ui/TransportControls.h"
#include "ui/ViewerWidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
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
    void notesPdfsForPlaylistExportOnePerClip();
    void notesComposerBelongsToTheFrame();
    void notesPanelSearchesAndSorts();
    void tracingPaperAndGhostingToggleAndPersist();
    void drawButtonAndCtrlAngleBracketsJumpBetweenDrawings();
    void drawingRowIsIconsWithAFloatingSizePill();
    void fileMenuHasTheNotesExports();
    void exportsOpenOnlyOnceTheFileIsReady();
    void watchingASavedProjectNeverAsksToSave();
    void savingAProjectSaysSo();
    void exportProgressWindowCancelsAndCloses();
    void copyCutAndPasteBetweenFrames();
    void dragTimelineMarkersToMoveOrCopyAFrame();
    void holdDrawingsUntilTheNextDrawing();
    void speedZoomAndFlipButtons();
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
    QVERIFY(panel && !panel->isVisible()); // closed at start, so the picture gets the room
    // The Notes button at the top right opens it, on the right; F6 toggles it.
    auto* notesButton = window.findChild<QToolButton*>(QStringLiteral("NotesToggleButton"));
    QVERIFY(notesButton && notesButton->isVisible());
    QVERIFY(notesButton->mapTo(&window, QPoint(0, 0)).x() > window.width() / 2);
    notesButton->click();
    QTRY_VERIFY(panel->isVisible());
    QVERIFY(notesButton->isChecked());
    QVERIFY(panel->mapTo(&window, QPoint(0, 0)).x() > window.width() / 2);
    QCOMPARE(command(window, "view.toggleNotes")->shortcut(), QKeySequence(QStringLiteral("F6")));
    command(window, "view.toggleNotes")->trigger();
    QTRY_VERIFY(!panel->isVisible());
    QVERIFY(!notesButton->isChecked());
    command(window, "view.toggleNotes")->trigger();
    QTRY_VERIFY(panel->isVisible());
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
    bool textSwitchWorks = false;
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
            // "Include text notes" (on by default) switches to an images-only PDF.
            auto* includeText = dialog->findChild<QCheckBox*>(QStringLiteral("ReviewNotesIncludeText"));
            auto* includeResolved = dialog->findChild<QCheckBox*>(QStringLiteral("ReviewNotesIncludeResolved"));
            textSwitchWorks = includeText && includeResolved && includeText->isChecked();
            if (textSwitchWorks) {
                includeText->setChecked(false);
                textSwitchWorks = !includeResolved->isEnabled();
                includeText->setChecked(true);
                textSwitchWorks = textSwitchWorks && includeResolved->isEnabled();
            }
            dialog->findChild<QLineEdit*>(QStringLiteral("ReviewNotesDestination"))->setText(pdf);
            dialog->findChild<QPushButton*>(QStringLiteral("ReviewNotesExport"))->click();
        }
    });
    driver.start();
    exportPdf->click();
    QTRY_COMPARE_WITH_TIMEOUT(QFileInfo(window.lastOpenedExport()).absoluteFilePath(), QFileInfo(pdf).absoluteFilePath(), 20000);
    driver.stop();
    QVERIFY(sawDialog);
    QVERIFY(textSwitchWorks);
    QVERIFY(openOffered);   // on by default
    // The progress window has closed itself.
    QTRY_VERIFY(std::none_of(window.findChildren<atk::ui::ExportProgressDialog*>().cbegin(),
                             window.findChildren<atk::ui::ExportProgressDialog*>().cend(),
                             [](const QWidget* w) { return w->isVisible(); }));
    QVERIFY(tracingOffered);
    QCOMPARE(summary, QStringLiteral("1 frame(s) will be listed."));
    QFile file(pdf);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(4) == "%PDF");
    QVERIFY(!QFile::exists(pdf + QStringLiteral(".partial")));
}

void TestAnnotationWorkflow::notesPdfsForPlaylistExportOnePerClip()
{
    QTemporaryDir directory;
    // Four clips: two with notes, one without, and the first clip's file a
    // second time (its PDF must not overwrite the first one's).
    const auto copy = [&](const QString& name) {
        const QString path = directory.filePath(name);
        return QFile::copy(fixturePath(), path) ? path : QString();
    };
    const QString anna = copy(QStringLiteral("anna.mkv"));
    const QString ben = copy(QStringLiteral("ben.mkv"));
    const QString cara = copy(QStringLiteral("cara.mkv"));
    QVERIFY(!anna.isEmpty() && !ben.isEmpty() && !cara.isEmpty());
    const auto noted = [](qint64 frame, const QString& text, bool resolved = false) {
        atk::annotation::FrameAnnotation annotation;
        annotation.frame = frame;
        atk::annotation::FrameNote note;
        note.id = quint64(frame + 1);
        note.text = text;
        note.author = QStringLiteral("David");
        note.createdMsUtc = QDateTime::currentMSecsSinceEpoch();
        note.resolved = resolved;
        annotation.notes.append(note);
        return annotation;
    };
    atk::project::Project project;
    project.setName(QStringLiteral("Class"));
    for (const QString& clip : {anna, ben, cara, anna})
        project.addSource(std::make_shared<atk::media::MediaSource>(clip));
    project.mutableEntries()[0].annotations.setFrame(noted(10, QStringLiteral("watch the arc")));
    project.mutableEntries()[0].annotations.setFrame(noted(20, QStringLiteral("done"), true));
    project.mutableEntries()[1].annotations.setFrame(noted(5, QStringLiteral("spacing too large")));
    project.mutableEntries()[3].annotations.setFrame(noted(7, QStringLiteral("second look")));
    project.setActiveIndex(1);
    const QString projectPath = directory.filePath(QStringLiteral("class.atkproj"));
    QVERIFY(atk::project::ProjectSerializer::save(project, projectPath).ok);

    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 0));
    // Every clip read, so each has a frame count to export over.
    QTRY_VERIFY_WITH_TIMEOUT(std::all_of(window.project()->entries().cbegin(), window.project()->entries().cend(),
        [](const atk::project::SourceEntry& e) { return e.source && e.source->metadata().effectiveFrameCount() > 0; }),
        20000);
    QAction* batch = command(window, "file.exportReviewNotesBatch");
    QVERIFY(batch && batch->isEnabled());
    QCOMPARE(batch->text(), QStringLiteral("Export Notes PDFs for Playlist..."));

    const QString out = directory.filePath(QStringLiteral("pdfs"));
    int listed = 0;
    bool caraLocked = false;
    bool noneDisables = false;
    QString summary;
    QTimer driver;
    driver.setInterval(20);
    connect(&driver, &QTimer::timeout, this, [&] {
        auto* dialog = qobject_cast<atk::ui::BatchNotesPdfDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        driver.stop();
        auto* list = dialog->findChild<QListWidget*>(QStringLiteral("BatchNotesClips"));
        auto* exportButton = dialog->findChild<QPushButton*>(QStringLiteral("BatchNotesExport"));
        listed = list->count();
        caraLocked = !(list->item(2)->flags() & Qt::ItemIsEnabled) && list->item(2)->checkState() == Qt::Unchecked
                     && list->item(2)->text().contains(QStringLiteral("nothing to list"));
        dialog->findChild<QPushButton*>(QStringLiteral("BatchNotesSelectNone"))->click();
        noneDisables = !exportButton->isEnabled() && dialog->chosenClips().isEmpty();
        dialog->findChild<QPushButton*>(QStringLiteral("BatchNotesSelectAll"))->click();
        summary = dialog->findChild<QLabel*>(QStringLiteral("BatchNotesSummary"))->text();
        dialog->findChild<QLineEdit*>(QStringLiteral("BatchNotesFolder"))->setText(QDir::toNativeSeparators(out));
        exportButton->click();
    });
    driver.start();
    batch->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!window.notesPdfBatchInProgress(), 60000);
    QCOMPARE(listed, 4);
    QVERIFY(caraLocked);   // nothing to list: cannot be chosen
    QVERIFY(noneDisables);
    QCOMPARE(summary, QStringLiteral("3 PDF(s) will be saved, one per clip."));

    // One PDF per chosen clip, named after the clip; the repeat gets (2).
    QStringList names;
    for (const QString& path : window.lastNotesPdfBatchSaved()) {
        names.append(QFileInfo(path).fileName());
        QCOMPARE(QFileInfo(path).absolutePath(), QFileInfo(out).absoluteFilePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(4) == "%PDF");
    }
    QCOMPARE(names, (QStringList{QStringLiteral("anna_review_notes.pdf"), QStringLiteral("ben_review_notes.pdf"),
                                 QStringLiteral("anna_review_notes (2).pdf")}));
    QVERIFY(window.lastNotesPdfBatchFailed().isEmpty());
    QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("pdfs/cara_review_notes.pdf"))));
    QTRY_VERIFY(std::none_of(window.findChildren<atk::ui::ExportProgressDialog*>().cbegin(),
                             window.findChildren<atk::ui::ExportProgressDialog*>().cend(),
                             [](const QWidget* w) { return w->isVisible(); }));
    QVERIFY(batch->isEnabled());

    // Keep Both: the existing PDFs stay and the new one is numbered on.
    atk::exporter::ReviewNotesOptions options;
    const QUuid annaId = window.project()->entries().at(0).id;
    QVERIFY(window.exportNotesPdfs({annaId}, out, options, false));
    QTRY_VERIFY_WITH_TIMEOUT(!window.notesPdfBatchInProgress(), 30000);
    QCOMPARE(window.lastNotesPdfBatchSaved().size(), 1);
    QCOMPARE(QFileInfo(window.lastNotesPdfBatchSaved().first()).fileName(), QStringLiteral("anna_review_notes (3).pdf"));

    // Replace: written over the clip's own PDF.
    QVERIFY(window.exportNotesPdfs({annaId}, out, options, true));
    QTRY_VERIFY_WITH_TIMEOUT(!window.notesPdfBatchInProgress(), 30000);
    QCOMPARE(QFileInfo(window.lastNotesPdfBatchSaved().value(0)).fileName(), QStringLiteral("anna_review_notes.pdf"));

    // Only a numbered PDF left from before (the repeated clip's): the batch
    // still asks before writing over it, and Keep Both numbers on past it.
    QVERIFY(QFile::remove(QDir(out).filePath(QStringLiteral("anna_review_notes.pdf"))));
    QVERIFY(QFile::remove(QDir(out).filePath(QStringLiteral("ben_review_notes.pdf"))));
    QString asked;
    QTimer again;
    again.setInterval(20);
    connect(&again, &QTimer::timeout, this, [&] {
        QWidget* modal = QApplication::activeModalWidget();
        if (auto* dialog = qobject_cast<atk::ui::BatchNotesPdfDialog*>(modal)) {
            dialog->findChild<QLineEdit*>(QStringLiteral("BatchNotesFolder"))->setText(QDir::toNativeSeparators(out));
            dialog->findChild<QPushButton*>(QStringLiteral("BatchNotesExport"))->click();
        } else if (auto* box = qobject_cast<QMessageBox*>(modal)) {
            asked = box->text();
            for (QAbstractButton* button : box->buttons())
                if (button->text() == QStringLiteral("Keep Both")) button->click();
        }
    });
    again.start();
    batch->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!window.notesPdfBatchInProgress(), 60000);
    again.stop();
    QCOMPARE(asked, QStringLiteral("1 of these PDFs already exist in the folder."));
    QStringList kept;
    for (const QString& path : window.lastNotesPdfBatchSaved()) kept.append(QFileInfo(path).fileName());
    QCOMPARE(kept, (QStringList{QStringLiteral("anna_review_notes.pdf"), QStringLiteral("ben_review_notes.pdf"),
                                QStringLiteral("anna_review_notes (4).pdf")}));

    // A clip with nothing to list is reported, not exported.
    QVERIFY(!window.exportNotesPdfs({window.project()->entries().at(2).id}, out, options, true));
    QCOMPARE(window.lastNotesPdfBatchFailed().size(), 1);
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

void TestAnnotationWorkflow::notesPanelSearchesAndSorts()
{
    atk::ui::NotesPanel panel;
    auto row = [](qint64 frame, quint64 id, const char* text, const char* author, qint64 when) {
        atk::ui::NoteRow r;
        r.frame = frame;
        r.note.id = id;
        r.note.text = QString::fromLatin1(text);
        r.note.author = QString::fromLatin1(author);
        r.note.createdMsUtc = when;
        return r;
    };
    panel.setCurrentFrame(10, QStringLiteral("00:00:00:10"), true);
    panel.setNotes({row(30, 1, "Arc on the hand", "Selena", 3000), row(10, 2, "Hold the pose longer", "David", 1000),
                    row(20, 3, "Hand pops here", "David", 2000)});
    auto texts = [&] {
        QStringList shown;
        for (int i = 0; i < panel.list()->count(); ++i) shown << panel.list()->item(i)->text();
        return shown;
    };
    // Frame order by default.
    QCOMPARE(texts(), (QStringList{QStringLiteral("Hold the pose longer"), QStringLiteral("Hand pops here"),
                                   QStringLiteral("Arc on the hand")}));
    // Newest first from the sort button.
    auto* sortButton = panel.findChild<QToolButton*>(QStringLiteral("NotesSortButton"));
    QVERIFY(sortButton);
    sortButton->click();
    QCOMPARE(panel.sort(), atk::ui::NotesPanel::Sort::NewestFirst);
    QCOMPARE(texts().first(), QStringLiteral("Arc on the hand"));
    sortButton->click();
    QCOMPARE(texts().first(), QStringLiteral("Hold the pose longer"));

    // Search matches note text or author, any case, and combines with the filter.
    auto* searchButton = panel.findChild<QToolButton*>(QStringLiteral("NotesSearchButton"));
    auto* search = panel.findChild<QLineEdit*>(QStringLiteral("NotesSearch"));
    QVERIFY(searchButton && search && search->isHidden());
    searchButton->click();
    QVERIFY(!search->isHidden());
    search->setText(QStringLiteral("HAND"));
    QCOMPARE(texts(), (QStringList{QStringLiteral("Hand pops here"), QStringLiteral("Arc on the hand")}));
    search->setText(QStringLiteral("selena"));
    QCOMPARE(texts(), QStringList{QStringLiteral("Arc on the hand")});
    panel.setFilter(atk::ui::NotesPanel::Filter::ThisFrame);
    QCOMPARE(panel.list()->count(), 0); // Selena's note is not on frame 10
    panel.setFilter(atk::ui::NotesPanel::Filter::All);
    // Closing search clears it, so no note stays hidden by an invisible filter.
    searchButton->click();
    QVERIFY(panel.searchText().isEmpty());
    QCOMPARE(panel.list()->count(), 3);
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
        // Ghost settings live with the view tools under the timeline.
        auto* before = window.findChild<QSpinBox*>(QStringLiteral("GhostBefore"));
        QVERIFY(toolbar->ghostButton()->isVisible() && toolbar->tracingPaperButton()->isVisible());
        QVERIFY(!toolbar->isAncestorOf(toolbar->ghostButton()));
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

void TestAnnotationWorkflow::drawingRowIsIconsWithAFloatingSizePill()
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
    auto* pill = window.findChild<atk::ui::BrushSizePill*>();
    QVERIFY(viewer && toolbar && pill);
    QVERIFY(!pill->isVisible());

    command(window, "draw.annotateMode")->trigger();
    QVERIFY(toolbar->isVisible());
    QTRY_VERIFY2(pill->isVisible(),
                 qPrintable(QStringLiteral("viewer %1x%2, pill hint %3x%4, window %5x%6")
                                .arg(viewer->width()).arg(viewer->height())
                                .arg(pill->sizeHint().width()).arg(pill->sizeHint().height())
                                .arg(window.width()).arg(window.height())));
    // Beside the picture: over the viewer's left edge, vertically inside it.
    const QRect pillRect(pill->mapTo(&window, QPoint(0, 0)), pill->size());
    const QRect viewerRect(viewer->mapTo(&window, QPoint(0, 0)), viewer->size());
    QVERIFY(viewerRect.contains(pillRect));
    QVERIFY(pillRect.left() - viewerRect.left() < 40);

    // Every tool is an icon button that keeps its name for menus and screen readers.
    QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
    for (const QString& name : {QStringLiteral("Select"), QStringLiteral("Pencil"), QStringLiteral("Pen"),
                               QStringLiteral("Brush"), QStringLiteral("Marker"), QStringLiteral("Custom"),
                               QStringLiteral("Eraser"), QStringLiteral("Arrow"), QStringLiteral("Rect"),
                               QStringLiteral("Ellipse"), QStringLiteral("Line"), QStringLiteral("Text")}) {
        QAction* tool = nullptr;
        for (QAction* action : toolbar->actions())
            if (action->text() == name) tool = action;
        QVERIFY2(tool && !tool->icon().isNull(), qPrintable(name));
    }
    // Undo, redo and clear are the window's own commands, shown on the row.
    for (const char* key : {"draw.undo", "draw.redo", "draw.clearFrame"})
        QVERIFY2(toolbar->actions().contains(command(window, key)), key);

    // The pill sets the brush size and opacity the viewer draws with.
    auto* sizeField = pill->findChild<QSpinBox*>(QStringLiteral("BrushSizeField"));
    auto* opacityField = pill->findChild<QSpinBox*>(QStringLiteral("BrushOpacityField"));
    QVERIFY(sizeField && opacityField);
    sizeField->setValue(23);
    QCOMPARE(viewer->annotationTool().sizePixels, 23.0);
    opacityField->setValue(40);
    QCOMPARE(viewer->annotationTool().opacity, 0.4);
    // The [ ] keys move the pill too.
    command(window, "draw.brushSizeUp")->trigger();
    QCOMPARE(sizeField->value(), qRound(toolbar->settings().sizePixels)); // whole pixels shown
    QVERIFY(toolbar->settings().sizePixels > 23.0);

    // With the Eraser chosen the pill sets the eraser's own size; opacity does not apply.
    const qreal brushSize = toolbar->settings().sizePixels;
    for (QAction* action : toolbar->actions())
        if (action->text() == QStringLiteral("Eraser")) action->trigger();
    QVERIFY(!opacityField->isEnabled());
    sizeField->setValue(60);
    QCOMPARE(viewer->annotationTool().eraserSizePixels, 60.0);
    QCOMPARE(viewer->annotationTool().sizePixels, brushSize);

    // Eraser mode, pressure and stabiliser live in the Tool options pop-up.
    QToolButton* options = toolbar->toolOptionsButton();
    QVERIFY(options && options->isVisible() && options->menu());
    auto* eraserMode = options->menu()->findChild<QComboBox*>(QStringLiteral("EraserModeCombo"));
    QVERIFY(eraserMode);
    eraserMode->setCurrentIndex(eraserMode->findData(int(atk::annotation::EraserMode::WholeStroke)));
    QCOMPARE(viewer->annotationTool().eraserMode, atk::annotation::EraserMode::WholeStroke);
    QVERIFY(options->menu()->findChild<QCheckBox*>());

    // Leaving Draw on Frame takes the pill away with the toolbar.
    command(window, "draw.annotateMode")->trigger();
    QVERIFY(!toolbar->isVisible());
    QTRY_VERIFY(!pill->isVisible());
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

void TestAnnotationWorkflow::savingAProjectSaysSo()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    QVERIFY(!projectPath.isEmpty());
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1280, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 0));
    window.statusBar()->clearMessage();
    command(window, "file.saveProject")->trigger();
    // The save names the file it wrote, so the user knows it landed.
    const QString message = window.statusBar()->currentMessage();
    QVERIFY2(message.startsWith(QStringLiteral("Saved draw.atkproj at ")), qPrintable(message));
    QVERIFY(!window.project()->isModified());
    QVERIFY(QApplication::overrideCursor() == nullptr);

    // And it is on screen: the frame readout fills the status bar, which left
    // QStatusBar no room to draw any message at all (0.5.4).
    auto* shown = window.findChild<QLabel*>(QStringLiteral("StatusMessage"));
    QVERIFY(shown);
    QCOMPARE(shown->text(), message);
    QVERIFY(window.statusBar()->isVisible() && shown->isVisible());
    QTRY_VERIFY(shown->width() >= shown->fontMetrics().horizontalAdvance(message));

    // From the Review Library too, where the status bar is otherwise hidden.
    window.showReviewLibrary();
    QVERIFY(!window.statusBar()->isVisible());
    command(window, "file.saveProject")->trigger();
    QVERIFY(shown->text().startsWith(QStringLiteral("Saved draw.atkproj at ")));
    QVERIFY(window.statusBar()->isVisible() && shown->isVisible());
    QVERIFY(!window.findChild<QLabel*>(QStringLiteral("StatusFrameValue"))->isVisible()); // no frame to report there
    QTRY_VERIFY(shown->width() >= shown->fontMetrics().horizontalAdvance(shown->text()));
    window.statusBar()->clearMessage();
    QVERIFY(!window.statusBar()->isVisible());

    // Back in the player, the readout returns.
    window.showPlayer();
    QVERIFY(window.statusBar()->isVisible());
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("StatusFrameValue"))->isVisible());
}

void TestAnnotationWorkflow::exportProgressWindowCancelsAndCloses()
{
    atk::ui::ExportProgressDialog progress(QStringLiteral("MP4"), QStringLiteral("Exporting video"),
                                           QStringLiteral("C:/out/review.mp4"));
    progress.show();
    QVERIFY(QTest::qWaitForWindowExposed(&progress));
    QSignalSpy cancelled(&progress, &atk::ui::ExportProgressDialog::canceled);
    progress.setLabelText(QStringLiteral("Rendering frame 12 / 160…"));
    progress.setValue(40);
    QCOMPARE(progress.value(), 40);
    QCOMPARE(progress.findChild<QLabel*>(QStringLiteral("ExportPercent"))->text(), QStringLiteral("40%"));
    QCOMPARE(progress.findChild<QLabel*>(QStringLiteral("ExportProgressStatus"))->text(),
             QStringLiteral("Rendering frame 12 / 160…"));

    // Cancel asks once and keeps the window up until the export has stopped.
    auto* cancel = progress.findChild<QPushButton*>(QStringLiteral("ExportProgressCancel"));
    cancel->click();
    QCOMPARE(cancelled.size(), 1);
    QVERIFY(progress.isVisible());
    QVERIFY(!cancel->isEnabled());
    progress.close(); // the title-bar close button asks too, but only once
    QCOMPARE(cancelled.size(), 1);
    QVERIFY(progress.isVisible());
    progress.finish();
    QVERIFY(!progress.isVisible());
}

namespace {
void seekAndWait(atk::ui::MainWindow& window, qint64 frame)
{
    window.playbackController()->seekFrame(frame);
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), frame, 5000);
    auto* panel = window.findChild<atk::ui::NotesPanel*>();
    QTRY_COMPARE(panel->currentFrame(), frame);
}

/// Drags the drawing marker under the timeline from one frame to another.
bool dragMarker(atk::ui::TimelineWidget* timeline, qint64 from, qint64 to, Qt::KeyboardModifiers modifiers)
{
    const int x = timeline->positionForFrame(from);
    int y = -1;
    for (int candidate = 0; candidate < timeline->height(); ++candidate)
        if (timeline->annotatedFrameAtPosition(QPoint(x, candidate)) == from) { y = candidate; break; }
    if (y < 0) return false;
    QTest::mousePress(timeline, Qt::LeftButton, modifiers, QPoint(x, y));
    const QPoint end(timeline->positionForFrame(to), y);
    for (int i = 1; i <= 4; ++i) {
        const QPoint p(x + (end.x() - x) * i / 4, y);
        QMouseEvent move(QEvent::MouseMove, p, timeline->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(timeline, &move);
    }
    const bool picked = timeline->markerDropFrame() == to;
    QTest::mouseRelease(timeline, Qt::LeftButton, modifiers, end);
    return picked;
}
} // namespace

void TestAnnotationWorkflow::copyCutAndPasteBetweenFrames()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 30));
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();
    auto* panel = window.findChild<atk::ui::NotesPanel*>();
    QVERIFY(viewer && toolbar && panel);
    const auto annotations = [&] { return window.project()->entries().at(0).annotations; };

    // Copy, cut and paste are buttons on the drawing row and the usual keys.
    command(window, "draw.annotateMode")->trigger();
    QAction* copy = command(window, "draw.copy");
    QAction* cut = command(window, "draw.cut");
    QAction* paste = command(window, "draw.paste");
    QVERIFY(copy && cut && paste);
    for (QAction* action : {copy, cut, paste}) {
        QVERIFY2(toolbar->actions().contains(action), qPrintable(action->objectName()));
        QVERIFY(!action->icon().isNull());
    }
    QCOMPARE(copy->shortcut(), QKeySequence(QStringLiteral("Ctrl+C")));
    QCOMPARE(cut->shortcut(), QKeySequence(QStringLiteral("Ctrl+X")));
    QCOMPARE(paste->shortcut(), QKeySequence(QStringLiteral("Ctrl+V")));
    QVERIFY(!paste->isEnabled()); // nothing copied yet

    // A drawing and a note on frame 30.
    drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
    panel->composer()->setFocus();
    QTest::keyClicks(panel->composer(), QStringLiteral("hold longer"));
    QTest::keyClick(panel->composer(), Qt::Key_Return, Qt::ControlModifier);
    const atk::annotation::FrameAnnotation frame30 = annotations().frame(30);
    QCOMPARE(frame30.items.size(), 1);
    QCOMPARE(frame30.notes.size(), 1);

    // With nothing selected, Copy takes the whole frame; Paste puts it on
    // another frame in the same place, as one undo step.
    copy->trigger();
    QVERIFY(paste->isEnabled());
    seekAndWait(window, 40);
    paste->trigger();
    const atk::annotation::FrameAnnotation frame40 = annotations().frame(40);
    QCOMPARE(frame40.items.size(), 1);
    QCOMPARE(frame40.notes.size(), 1);
    QCOMPARE(frame40.items[0].points, frame30.items[0].points);
    QVERIFY(frame40.items[0].id != frame30.items[0].id);
    QCOMPARE(frame40.notes[0].text, QStringLiteral("hold longer"));
    QCOMPARE(annotations().frame(30), frame30);
    QTRY_COMPARE(viewer->annotationFrame().items.size(), 1);
    QCOMPARE(panel->notes().size(), 2);
    command(window, "draw.undo")->trigger();
    QVERIFY(!annotations().hasFrame(40));

    // Pick one drawing with the Select tool and cut it: only it goes, the
    // note stays. Pasted on another frame it comes up selected, ready to drag.
    seekAndWait(window, 30);
    QTRY_COMPARE(viewer->annotationFrame().items.size(), 1);
    auto tool = toolbar->settings();
    tool.tool = atk::annotation::AnnotationTool::Select;
    toolbar->setSettings(tool);
    viewer->setAnnotationTool(tool);
    const QPoint onInk = viewer->normalizedToWidget({0.45, 0.45}).toPoint();
    QTest::mouseClick(viewer, Qt::LeftButton, {}, onInk);
    QCOMPARE(viewer->selectedItemId(), frame30.items[0].id);
    cut->trigger();
    QVERIFY(annotations().frame(30).items.isEmpty());
    QCOMPARE(annotations().frame(30).notes.size(), 1);
    seekAndWait(window, 50);
    paste->trigger();
    QCOMPARE(annotations().frame(50).items.size(), 1);
    QVERIFY(annotations().frame(50).notes.isEmpty());
    QCOMPARE(viewer->selectedItemId(), annotations().frame(50).items[0].id);
    drawStroke(viewer, {0.45, 0.45}, {0.55, 0.65}); // a drag moves the pasted drawing
    QVERIFY(annotations().frame(50).items[0].points != frame30.items[0].points);

    // Copy Note from the notes panel, then paste on another frame.
    emit panel->copyNoteRequested(30, frame30.notes[0].id, false);
    seekAndWait(window, 60);
    paste->trigger();
    QCOMPARE(annotations().frame(60).notes.size(), 1);
    QVERIFY(annotations().frame(60).items.isEmpty());
    QCOMPARE(annotations().frame(60).notes[0].text, QStringLiteral("hold longer"));
    // Cut Note removes it from its frame.
    emit panel->copyNoteRequested(60, annotations().frame(60).notes[0].id, true);
    QVERIFY(!annotations().hasFrame(60));

    // Copy with an empty frame copies nothing and keeps the clipboard.
    seekAndWait(window, 70);
    copy->trigger();
    paste->trigger();
    QCOMPARE(annotations().frame(70).notes.size(), 1);

    // In a text field the keys keep their usual meaning: Ctrl+X and Ctrl+V
    // edit the text and leave the drawings alone.
    QVERIFY(QTest::qWaitForWindowActive(&window));
    const atk::annotation::SourceAnnotations beforeTyping = annotations();
    panel->composer()->setFocus();
    QTest::keyClicks(panel->composer(), QStringLiteral("abc"));
    QTest::keyClick(panel->composer(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(panel->composer(), Qt::Key_X, Qt::ControlModifier);
    QVERIFY(panel->composer()->toPlainText().isEmpty());
    QTest::keyClick(panel->composer(), Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(panel->composer()->toPlainText(), QStringLiteral("abc"));
    QCOMPARE(annotations(), beforeTyping);
    // Over the picture, Ctrl+V pastes.
    viewer->setFocus();
    QTest::keyClick(viewer, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(annotations().frame(70).notes.size(), 2);
}

void TestAnnotationWorkflow::dragTimelineMarkersToMoveOrCopyAFrame()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 30));
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    auto* timeline = window.findChild<atk::ui::TimelineWidget*>();
    auto* panel = window.findChild<atk::ui::NotesPanel*>();
    QVERIFY(viewer && timeline && panel);
    const auto annotations = [&] { return window.project()->entries().at(0).annotations; };

    command(window, "draw.annotateMode")->trigger();
    drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
    panel->composer()->setFocus();
    QTest::keyClicks(panel->composer(), QStringLiteral("spacing"));
    QTest::keyClick(panel->composer(), Qt::Key_Return, Qt::ControlModifier);
    const atk::annotation::FrameAnnotation original = annotations().frame(30);
    QCOMPARE(timeline->annotatedFrames(), QVector<int64_t>({30}));

    // Drag Markers starts off, like SyncSketch: a marker cannot be picked up.
    QVERIFY(!timeline->isMarkerDragEnabled());
    QVERIFY(!command(window, "draw.dragMarkers")->isChecked());
    QVERIFY(!dragMarker(timeline, 30, 90, Qt::NoModifier));
    QCOMPARE(annotations().frame(30), original);
    command(window, "draw.dragMarkers")->trigger();
    QVERIFY(timeline->isMarkerDragEnabled());

    // Drag the marker: the frame's drawings and notes move, and the player
    // follows them.
    QVERIFY(dragMarker(timeline, 30, 90, Qt::NoModifier));
    QVERIFY(!annotations().hasFrame(30));
    QCOMPARE(annotations().frame(90).items, original.items);
    QCOMPARE(annotations().frame(90).notes, original.notes);
    QCOMPARE(timeline->annotatedFrames(), QVector<int64_t>({90}));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(90), 5000);
    QTRY_COMPARE(viewer->annotationFrame().items.size(), 1);

    // Ctrl+drag copies; the original stays.
    QVERIFY(dragMarker(timeline, 90, 120, Qt::ControlModifier));
    QCOMPARE(timeline->annotatedFrames(), QVector<int64_t>({90, 120}));
    QCOMPARE(annotations().frame(120).items.size(), 1);
    QCOMPARE(annotations().frame(120).notes.size(), 1);

    // Each drag is one undo step.
    command(window, "draw.undo")->trigger();
    QCOMPARE(timeline->annotatedFrames(), QVector<int64_t>({90}));
    command(window, "draw.undo")->trigger();
    QCOMPARE(timeline->annotatedFrames(), QVector<int64_t>({30}));
    QCOMPARE(annotations().frame(30), original);

    // A click on a marker without dragging just goes to that frame.
    seekAndWait(window, 10);
    const int x = timeline->positionForFrame(30);
    int y = -1;
    for (int candidate = 0; candidate < timeline->height() && y < 0; ++candidate)
        if (timeline->annotatedFrameAtPosition(QPoint(x, candidate)) == 30) y = candidate;
    QVERIFY(y >= 0);
    QTest::mouseClick(timeline, Qt::LeftButton, {}, QPoint(x, y));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->currentFrame(), qint64(30), 5000);
    QCOMPARE(annotations().frame(30), original);
}

void TestAnnotationWorkflow::holdDrawingsUntilTheNextDrawing()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    {
        atk::ui::MainWindow window(settingsPath);
        window.resize(1400, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(openAt(window, projectPath, 30));
        auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
        auto* timeline = window.findChild<atk::ui::TimelineWidget*>();
        QVERIFY(viewer && timeline);
        const auto annotations = [&] { return window.project()->entries().at(0).annotations; };
        const qint64 lastFrame = window.playbackController()->metadata().effectiveFrameCount() - 1;
        QVERIFY(lastFrame > 60);

        command(window, "draw.annotateMode")->trigger();
        drawStroke(viewer, {0.3, 0.4}, {0.6, 0.5});
        QCOMPARE(annotations().frame(30).items.size(), 1);

        // Off by default: a drawing shows on its own frame only.
        QVERIFY(!command(window, "draw.holdDrawings")->isChecked());
        QVERIFY(command(window, "draw.holdOff")->isChecked());
        seekAndWait(window, 40);
        QVERIFY(viewer->annotationFrame().items.isEmpty());
        QVERIFY(timeline->holdSpans().isEmpty());

        // On: frame 30's drawing holds over frame 40, and the timeline shows it.
        command(window, "draw.holdDrawings")->trigger();
        QVERIFY(command(window, "draw.holdAddToCurrent")->isChecked());
        QTRY_COMPARE(viewer->annotationFrame().items.size(), 1);
        QCOMPARE(timeline->holdSpans(), QVector<atk::annotation::HoldSpan>({{30, lastFrame}}));

        // Add to Current: a stroke on the held frame joins frame 30's drawing.
        drawStroke(viewer, {0.2, 0.2}, {0.4, 0.3});
        QCOMPARE(annotations().frame(30).items.size(), 2);
        QVERIFY(!annotations().hasFrame(40));
        QTRY_COMPARE(viewer->annotationFrame().items.size(), 2);
        command(window, "draw.undo")->trigger();
        QCOMPARE(annotations().frame(30).items.size(), 1);

        // Add New: a stroke on the held frame starts a drawing on frame 40,
        // which then holds from there.
        command(window, "draw.holdAddNew")->trigger();
        QVERIFY(command(window, "draw.holdDrawings")->isChecked());
        drawStroke(viewer, {0.2, 0.2}, {0.4, 0.3});
        QCOMPARE(annotations().frame(40).items.size(), 1);
        QCOMPARE(annotations().frame(30).items.size(), 1);
        QCOMPARE(timeline->holdSpans(), QVector<atk::annotation::HoldSpan>({{30, 39}, {40, lastFrame}}));

        // Erasing a held drawing erases it where it was drawn.
        seekAndWait(window, 45);
        QTRY_COMPARE(viewer->annotationFrame().items.size(), 1);
        command(window, "draw.clearFrame")->trigger();
        QVERIFY(!annotations().hasFrame(40));
        QVERIFY(!annotations().hasFrame(45));
        QTRY_COMPARE(viewer->annotationFrame().items, annotations().frame(30).items);

        // Off and on again: the toggle remembers Add New.
        command(window, "draw.holdDrawings")->trigger();
        QTRY_VERIFY(viewer->annotationFrame().items.isEmpty());
        QVERIFY(timeline->holdSpans().isEmpty());
        command(window, "draw.holdDrawings")->trigger();
        QVERIFY(command(window, "draw.holdAddNew")->isChecked());
    }
    // The choice is saved.
    QSettings settings(settingsPath, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("draw/holdDrawings")).toString(), QStringLiteral("addNew"));
}

void TestAnnotationWorkflow::speedZoomAndFlipButtons()
{
    QTemporaryDir directory;
    const QString projectPath = writeProject(directory);
    atk::ui::MainWindow window(directory.filePath(QStringLiteral("settings.ini")));
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(openAt(window, projectPath, 30));
    auto* viewer = window.findChild<atk::ui::ViewerWidget*>();
    auto* speed = window.findChild<QToolButton*>(QStringLiteral("TransportSpeedButton"));
    auto* zoom = window.findChild<QToolButton*>(QStringLiteral("TransportZoomButton"));
    auto* toolbar = window.findChild<atk::ui::AnnotationToolbar*>();
    QVERIFY(viewer && speed && zoom && toolbar);
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("TransportHoldButton")));
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("TransportDragMarkersButton")));

    // The speed button names the rate it plays at, from the media's own rate.
    const atk::media::FrameRate rate = window.playbackController()->metadata().frameRate;
    QVERIFY(rate.isValid());
    QCOMPARE(speed->text(), atk::ui::TransportControls::speedLabel(1.0, rate));
    command(window, "playback.speedDown")->trigger();
    QCOMPARE(window.playbackController()->playbackSpeed(), 0.75);
    QCOMPARE(speed->text(), atk::ui::TransportControls::speedLabel(0.75, rate));
    command(window, "playback.speedReset")->trigger();
    QCOMPARE(window.playbackController()->playbackSpeed(), 1.0);

    // The zoom button shows the level; a menu preset zooms the viewer.
    QCOMPARE(zoom->text(), QStringLiteral("Fit"));
    QAction* twoHundred = nullptr;
    for (QAction* action : zoom->menu()->actions())
        if (action->text() == QStringLiteral("200%")) twoHundred = action;
    QVERIFY(twoHundred);
    twoHundred->trigger();
    QCOMPARE(qRound(viewer->transform().zoomRatio() * 100.0), 200);
    QCOMPARE(zoom->text(), QStringLiteral("200%"));
    command(window, "view.zoomFit")->trigger();
    QCOMPARE(zoom->text(), QStringLiteral("Fit"));

    // Flip canvas is a quick button on the drawing row as well.
    QAction* flip = command(window, "view.flipHorizontal");
    QVERIFY(toolbar->actions().contains(flip));
    flip->trigger();
    QVERIFY(viewer->isFlippedHorizontally());
    flip->trigger();
    QVERIFY(!viewer->isFlippedHorizontally());
}

QTEST_MAIN(TestAnnotationWorkflow)
#include "tst_annotationworkflow.moc"
