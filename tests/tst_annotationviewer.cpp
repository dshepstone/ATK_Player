#include "annotation/AnnotationRenderer.h"
#include "annotation/AnnotationStore.h"
#include "annotation/AnnotationTypes.h"
#include "media/VideoFrame.h"
#include "ui/ViewerWidget.h"

#include <QApplication>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>

#include <cmath>
#include <memory>

using namespace atk;
using annotation::AnnotationItem;
using annotation::AnnotationTool;
using ui::ViewerWidget;

Q_DECLARE_METATYPE(atk::annotation::AnnotationItem)

namespace {
media::VideoFrame frame(int64_t index = 12)
{
    media::VideoFrame result;
    result.frameIndex = index;
    result.width = 200;
    result.height = 100;
    result.image = QImage(200, 100, QImage::Format_RGB32);
    result.image.fill(Qt::black);
    return result;
}

std::unique_ptr<QPointingDevice> makeStylus(QPointingDevice::PointerType type)
{
    return std::make_unique<QPointingDevice>(
        type == QPointingDevice::PointerType::Eraser ? QStringLiteral("test-eraser")
                                                     : QStringLiteral("test-pen"),
        type == QPointingDevice::PointerType::Eraser ? 9002 : 9001, QInputDevice::DeviceType::Stylus,
        type, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
}

/// Sends one tablet event and reports whether the viewer accepted it.
bool sendTablet(QWidget* target, const QPointingDevice* device, QEvent::Type type, const QPointF& pos,
                qreal pressure, Qt::MouseButton button = Qt::LeftButton)
{
    const Qt::MouseButtons buttons = type == QEvent::TabletRelease ? Qt::NoButton : Qt::MouseButtons(button);
    QTabletEvent event(type, device, pos, target->mapToGlobal(pos), pressure, 0.0f, 0.0f, 0.0f, 0.0, 0.0f,
                       Qt::NoModifier, button, buttons);
    event.setAccepted(false);
    QApplication::sendEvent(target, &event);
    return event.isAccepted();
}

struct Fixture {
    ViewerWidget viewer;
    Fixture()
    {
        viewer.resize(400, 300);
        viewer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewer));
        viewer.setFrame(frame());
    }
    QPoint at(qreal nx, qreal ny) const { return viewer.normalizedToWidget({nx, ny}).toPoint(); }
};
/// Plays MainWindow's part for erase tests: applies the viewer's erase
/// requests to a store and shows the result, so the real geometry runs.
struct EraseOwner {
    annotation::AnnotationStore store;
    ViewerWidget& viewer;
    annotation::EraserMode mode = annotation::EraserMode::Area;
    explicit EraseOwner(ViewerWidget& v) : viewer(v)
    {
        QObject::connect(&viewer, &ViewerWidget::annotationEraseStarted, &store,
                         [this](qint64) { store.beginEditGroup(); });
        QObject::connect(&viewer, &ViewerWidget::annotationEraseFinished, &store,
                         [this] { store.endEditGroup(); });
        QObject::connect(&viewer, &ViewerWidget::annotationEraseRequested, &store,
                         [this](qint64 frame, const QVector<QPointF>& centers, qreal radius, const QSizeF& size) {
                             store.eraseDiscs(frame, centers, radius, size, mode);
                         });
        QObject::connect(&store, &annotation::AnnotationStore::frameChanged, &viewer,
                         [this](qint64 frame) { viewer.setAnnotationFrame(store.frame(frame)); });
    }
    void add(qint64 frame, const AnnotationItem& item)
    {
        store.addItem(frame, 0, item);
        store.clearHistory();
    }
};

AnnotationItem horizontalLine()
{
    AnnotationItem line;
    line.tool = AnnotationTool::Pen;
    line.size = 0.02; // 2 px of a 100 px tall image
    line.points = {{0.1f, 0.5f, 1, 0, 0}, {0.9f, 0.5f, 1, 0, 0}};
    return line;
}
} // namespace

class TestAnnotationViewer : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<atk::annotation::AnnotationItem>(); }
    void mouseStrokeOutsideAnnotateModeDoesNothing();
    void mouseStrokeProducesNormalizedItemOnDisplayedFrame();
    void pressOnLetterboxDoesNotStartAStroke();
    void tabletPressureIsRecordedAndEventsAreAccepted();
    void stylusEraserEndErasesInsteadOfDrawing();
    void barrelButtonsFallThroughToMouseHandling();
    void fastEraserDragCannotJumpALine();
    void wholeStrokeModeRemovesTouchedStrokes();
    void eraserHasItsOwnSize();
    void flippedViewStoresUnflippedCoordinates();
    void zoomedViewMapsToTheSameImagePoint();
    void shapeNeedsADrag();
    void textClickTypesInPlaceAndCommits();
    void addingStrokesPaintsOnlyTheNewOne();
    void textDragMakesAWrappingBox();
    void textEscapeAndEmptyTextAddNothing();
    void clickingExistingTextEditsIt();
    void selectDragMovesAnItem();
    void deleteKeyRemovesTheSelection();
    void doublePressOnTextWithSelectEdits();
    void changingFrameFinishesText();
    void typedTextShowsAtItsFinalSize();
    void textBoxResizesAndMovesWhileTyping();
    void leavingAnnotateModeCancelsAStroke();
    void doubleClickDoesNotFitWhileAnnotating();
    void annotationsPaintOnlyOnTheirFrame();
    void addedStrokesExtendTheCacheExactly();
    void tracingPaperWashesThePictureButNotTheDrawings();
    void ghostsPaintTintedAndFadedUnderTheFrame();
};

void TestAnnotationViewer::mouseStrokeOutsideAnnotateModeDoesNothing()
{
    Fixture f;
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.2, 0.5));
    QTest::mouseMove(&f.viewer, f.at(0.6, 0.5));
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.6, 0.5));
    QCOMPARE(drawn.count(), 0);
    QVERIFY(!f.viewer.isDrawing());
}

void TestAnnotationViewer::mouseStrokeProducesNormalizedItemOnDisplayedFrame()
{
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Pencil;
    tool.sizePixels = 10;
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);

    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.25, 0.5));
    QVERIFY(f.viewer.isDrawing());
    for (int i = 1; i <= 10; ++i) {
        QMouseEvent move(QEvent::MouseMove, f.at(0.25 + 0.05 * i, 0.5), f.viewer.mapToGlobal(f.at(0.25 + 0.05 * i, 0.5)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&f.viewer, &move);
    }
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.75, 0.5));

    QCOMPARE(drawn.count(), 1);
    QCOMPARE(drawn.at(0).at(0).toLongLong(), qint64(12)); // the displayed frame
    const auto item = drawn.at(0).at(1).value<AnnotationItem>();
    QCOMPARE(item.tool, AnnotationTool::Pencil);
    QVERIFY(item.points.size() >= 5);
    QVERIFY(std::abs(item.points.constFirst().x - 0.25f) < 0.02f);
    QVERIFY(std::abs(item.points.constLast().x - 0.75f) < 0.02f);
    QVERIFY(std::abs(item.points.constFirst().y - 0.5f) < 0.02f);
    for (const auto& p : item.points) QCOMPARE(p.pressure, 1.0f); // mouse = full pressure
    QVERIFY(std::abs(item.size - 10.0 / 100.0) < 1e-9);           // 10 source px of a 100 px tall image
    QVERIFY(!f.viewer.isDrawing());
}

void TestAnnotationViewer::pressOnLetterboxDoesNotStartAStroke()
{
    Fixture f;
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    // 200x100 in 400x300 fits as 400x200 with bars top and bottom.
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, QPoint(200, 10));
    QVERIFY(!f.viewer.isDrawing());
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, QPoint(200, 10));
    QCOMPARE(drawn.count(), 0);
}

void TestAnnotationViewer::tabletPressureIsRecordedAndEventsAreAccepted()
{
    Fixture f;
    f.viewer.setAnnotateMode(true);
    auto pen = makeStylus(QPointingDevice::PointerType::Pen);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);

    QVERIFY(sendTablet(&f.viewer, pen.get(), QEvent::TabletPress, f.at(0.1, 0.5), 0.1));
    for (int i = 1; i <= 8; ++i)
        QVERIFY(sendTablet(&f.viewer, pen.get(), QEvent::TabletMove, f.at(0.1 + 0.1 * i, 0.5), 0.1 + 0.1 * i));
    QVERIFY(sendTablet(&f.viewer, pen.get(), QEvent::TabletRelease, f.at(0.9, 0.5), 0.9));

    QCOMPARE(drawn.count(), 1);
    const auto item = drawn.at(0).at(1).value<AnnotationItem>();
    QVERIFY(item.points.size() >= 5);
    QVERIFY(item.points.constFirst().pressure < 0.2f);
    QVERIFY(item.points.constLast().pressure > 0.8f);
    for (int i = 1; i < item.points.size(); ++i)
        QVERIFY(item.points[i].pressure >= item.points[i - 1].pressure); // monotonic ramp kept

    // A mouse event arriving mid-tablet-stroke is ignored, not doubled up.
    QVERIFY(sendTablet(&f.viewer, pen.get(), QEvent::TabletPress, f.at(0.2, 0.3), 0.5));
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.8, 0.8));
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.8, 0.8));
    QVERIFY(f.viewer.isDrawing());
    QVERIFY(sendTablet(&f.viewer, pen.get(), QEvent::TabletRelease, f.at(0.3, 0.3), 0.5));
    QCOMPARE(drawn.count(), 2);
    for (const auto& p : drawn.at(1).at(1).value<AnnotationItem>().points) QVERIFY(p.y < 0.5f);

    // Outside annotate mode the tablet is left to Qt's mouse synthesis.
    f.viewer.setAnnotateMode(false);
    QVERIFY(!sendTablet(&f.viewer, pen.get(), QEvent::TabletPress, f.at(0.5, 0.5), 0.5));
}

void TestAnnotationViewer::stylusEraserEndErasesInsteadOfDrawing()
{
    Fixture f;
    f.viewer.setAnnotateMode(true); // the Pen tool is selected...
    EraseOwner owner(f.viewer);
    owner.add(12, horizontalLine());

    auto eraser = makeStylus(QPointingDevice::PointerType::Eraser); // ...but the pen is flipped
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    QSignalSpy started(&f.viewer, &ViewerWidget::annotationEraseStarted);
    QSignalSpy finished(&f.viewer, &ViewerWidget::annotationEraseFinished);
    QVERIFY(sendTablet(&f.viewer, eraser.get(), QEvent::TabletPress, f.at(0.5, 0.3), 1.0));
    QVERIFY(sendTablet(&f.viewer, eraser.get(), QEvent::TabletMove, f.at(0.5, 0.7), 1.0)); // across the line
    QVERIFY(sendTablet(&f.viewer, eraser.get(), QEvent::TabletRelease, f.at(0.5, 0.7), 0.0));
    QCOMPARE(drawn.count(), 0);
    QCOMPARE(started.count(), 1);
    QCOMPARE(finished.count(), 1);

    // Area erase: the line is cut in two around x = 0.5, not deleted.
    const auto items = owner.store.frame(12).items;
    QCOMPARE(items.size(), 2);
    float leftMax = 0, rightMin = 1;
    for (const auto& p : items[0].points) leftMax = std::max(leftMax, p.x);
    for (const auto& p : items[1].points) rightMin = std::min(rightMin, p.x);
    QVERIFY2(leftMax < 0.5f && rightMin > 0.5f, qPrintable(QStringLiteral("%1 %2").arg(leftMax).arg(rightMin)));
    QVERIFY(leftMax > 0.35f && rightMin < 0.65f); // only a small gap was removed

    // The whole swipe is one undo step.
    QVERIFY(owner.store.undo());
    QCOMPARE(owner.store.frame(12).items.size(), 1);
    QVERIFY(!owner.store.canUndo());
}

void TestAnnotationViewer::wholeStrokeModeRemovesTouchedStrokes()
{
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Eraser;
    tool.eraserMode = annotation::EraserMode::WholeStroke;
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    EraseOwner owner(f.viewer);
    owner.mode = annotation::EraserMode::WholeStroke;
    owner.add(12, horizontalLine());
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.5));
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.5));
    QVERIFY(owner.store.frame(12).isEmpty());
}

void TestAnnotationViewer::eraserHasItsOwnSize()
{
    annotation::AnnotationToolSettings tool;
    tool.sizePixels = 4;
    tool.eraserSizePixels = 40;
    QCOMPARE(tool.activeSizePixels(), 4.0);
    tool.tool = AnnotationTool::Eraser;
    QCOMPARE(tool.activeSizePixels(), 40.0);
    tool.setActiveSizePixels(60);
    QCOMPARE(tool.eraserSizePixels, 60.0);
    QCOMPARE(tool.sizePixels, 4.0);
    QCOMPARE(tool.eraserRadius(1.0f), 30.0);
    QVERIFY(tool.eraserRadius(0.1f) < tool.eraserRadius(1.0f)); // light touch, smaller eraser
    tool.usePressure = false;
    QCOMPARE(tool.eraserRadius(0.1f), 30.0);
}

void TestAnnotationViewer::fastEraserDragCannotJumpALine()
{
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Eraser;
    tool.eraserSizePixels = 2; // a small eraser
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    EraseOwner owner(f.viewer);
    owner.add(12, horizontalLine());
    // One big jump from above the line to below it, with no sample on it.
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.1));
    QMouseEvent move(QEvent::MouseMove, f.at(0.5, 0.9), f.viewer.mapToGlobal(f.at(0.5, 0.9)), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&f.viewer, &move);
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.9));
    QCOMPARE(owner.store.frame(12).items.size(), 2);
}

void TestAnnotationViewer::barrelButtonsFallThroughToMouseHandling()
{
    Fixture f;
    f.viewer.setAnnotateMode(true);
    auto pen = makeStylus(QPointingDevice::PointerType::Pen);
    QVERIFY(!sendTablet(&f.viewer, pen.get(), QEvent::TabletPress, f.at(0.5, 0.5), 0.5, Qt::MiddleButton));
    QVERIFY(!sendTablet(&f.viewer, pen.get(), QEvent::TabletPress, f.at(0.5, 0.5), 0.5, Qt::RightButton));
    QVERIFY(!f.viewer.isDrawing());
}

void TestAnnotationViewer::flippedViewStoresUnflippedCoordinates()
{
    Fixture f;
    f.viewer.setFlipHorizontal(true);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    // Widget-left of the flipped picture is image-right.
    const QRectF target = f.viewer.transform().imageRect();
    const QPoint left(int(target.left() + target.width() * 0.1), int(target.center().y()));
    const QPoint right(int(target.left() + target.width() * 0.2), int(target.center().y()));
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, left);
    QMouseEvent move(QEvent::MouseMove, right, f.viewer.mapToGlobal(right), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&f.viewer, &move);
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, right);
    QCOMPARE(drawn.count(), 1);
    const auto item = drawn.at(0).at(1).value<AnnotationItem>();
    QVERIFY(std::abs(item.points.constFirst().x - 0.9f) < 0.02f);
    QVERIFY(std::abs(item.points.constLast().x - 0.8f) < 0.02f);
}

void TestAnnotationViewer::zoomedViewMapsToTheSameImagePoint()
{
    Fixture f;
    const QPointF before = f.viewer.widgetToNormalized(f.viewer.normalizedToWidget({0.3, 0.6}));
    f.viewer.zoomIn();
    f.viewer.zoomIn();
    const QPointF widget = f.viewer.normalizedToWidget({0.3, 0.6});
    const QPointF after = f.viewer.widgetToNormalized(widget);
    QVERIFY(std::abs(before.x() - 0.3) < 1e-9 && std::abs(after.x() - 0.3) < 1e-9);
    QVERIFY(std::abs(after.y() - 0.6) < 1e-9);
}

void TestAnnotationViewer::shapeNeedsADrag()
{
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Rectangle;
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);

    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.5));
    QCOMPARE(drawn.count(), 0);

    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.2, 0.2));
    QMouseEvent move(QEvent::MouseMove, f.at(0.6, 0.7), f.viewer.mapToGlobal(f.at(0.6, 0.7)), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&f.viewer, &move);
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.6, 0.7));
    QCOMPARE(drawn.count(), 1);
    const auto item = drawn.at(0).at(1).value<AnnotationItem>();
    QCOMPARE(item.tool, AnnotationTool::Rectangle);
    QCOMPARE(item.points.size(), 2);
    QVERIFY(std::abs(item.points[1].x - 0.6f) < 0.02f && std::abs(item.points[1].y - 0.7f) < 0.02f);
}

namespace {
void useTool(ViewerWidget& viewer, AnnotationTool tool)
{
    annotation::AnnotationToolSettings settings = viewer.annotationTool();
    settings.tool = tool;
    viewer.setAnnotationTool(settings);
}

void drag(ViewerWidget& viewer, QPoint from, QPoint to)
{
    QTest::mousePress(&viewer, Qt::LeftButton, {}, from);
    for (int i = 1; i <= 4; ++i) {
        const QPoint p = from + (to - from) * i / 4;
        QMouseEvent move(QEvent::MouseMove, p, viewer.mapToGlobal(p), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&viewer, &move);
    }
    QTest::mouseRelease(&viewer, Qt::LeftButton, {}, to);
}

AnnotationItem textItem(quint64 id, const QString& text, float x = 0.3f, float y = 0.3f)
{
    AnnotationItem item;
    item.id = id;
    item.tool = AnnotationTool::Text;
    item.size = 0.03;
    item.text = text;
    item.points = {{x, y, 1, 0, 0}};
    return item;
}
} // namespace

void TestAnnotationViewer::textClickTypesInPlaceAndCommits()
{
    Fixture f;
    useTool(f.viewer, AnnotationTool::Text);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);

    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.4, 0.3));
    QVERIFY(f.viewer.isEditingText());
    QTextEdit* editor = f.viewer.textEditor();
    QVERIFY(editor && editor->isVisible());
    // The caret sits where the reviewer clicked.
    QVERIFY((editor->geometry().topLeft() - f.at(0.4, 0.3)).manhattanLength() <= 2);
    QTRY_VERIFY(editor->hasFocus());

    QTest::keyClicks(editor, QStringLiteral("fix the nose"));
    QTest::keyClick(editor, Qt::Key_Return); // Enter is a new line, not "done"
    QTest::keyClicks(editor, QStringLiteral("and the chin"));
    QVERIFY(f.viewer.isEditingText());
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier); // Ctrl+Enter finishes

    QVERIFY(!f.viewer.isEditingText());
    QCOMPARE(drawn.count(), 1);
    QCOMPARE(drawn.at(0).at(0).toLongLong(), qint64(12));
    const auto item = drawn.at(0).at(1).value<AnnotationItem>();
    QCOMPARE(item.tool, AnnotationTool::Text);
    QCOMPARE(item.text, QStringLiteral("fix the nose\nand the chin"));
    QCOMPARE(item.points.size(), 1); // point text
    QVERIFY(std::abs(item.points[0].x - 0.4f) < 0.02f && std::abs(item.points[0].y - 0.3f) < 0.02f);
}

void TestAnnotationViewer::textDragMakesAWrappingBox()
{
    Fixture f;
    useTool(f.viewer, AnnotationTool::Text);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);

    drag(f.viewer, f.at(0.2, 0.2), f.at(0.6, 0.5));
    QVERIFY(f.viewer.isEditingText());
    const int boxWidth = f.at(0.6, 0.2).x() - f.at(0.2, 0.2).x();
    QVERIFY(std::abs(f.viewer.textEditor()->width() - boxWidth) <= 4);
    QTest::keyClicks(f.viewer.textEditor(), QStringLiteral("a long note that should wrap inside the box"));
    // Clicking elsewhere on the picture finishes the text (and does nothing else).
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.9, 0.9));
    QVERIFY(!f.viewer.isEditingText());
    QCOMPARE(drawn.count(), 1);
    const auto item = drawn.at(0).at(1).value<AnnotationItem>();
    QVERIFY(annotation::isBoxText(item));
    QVERIFY(std::abs(item.points[0].x - 0.2f) < 0.02f && std::abs(item.points[1].x - 0.6f) < 0.02f);
}

void TestAnnotationViewer::textEscapeAndEmptyTextAddNothing()
{
    Fixture f;
    useTool(f.viewer, AnnotationTool::Text);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.4, 0.3));
    QTest::keyClicks(f.viewer.textEditor(), QStringLiteral("never mind"));
    QTest::keyClick(f.viewer.textEditor(), Qt::Key_Escape);
    QVERIFY(!f.viewer.isEditingText());
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.5));
    f.viewer.commitTextEdit(); // nothing typed
    QCOMPARE(drawn.count(), 0);
}

void TestAnnotationViewer::clickingExistingTextEditsIt()
{
    Fixture f;
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    existing.items = {textItem(9, QStringLiteral("old note"))};
    f.viewer.setAnnotationFrame(existing);
    useTool(f.viewer, AnnotationTool::Text);
    f.viewer.setAnnotateMode(true);
    QSignalSpy changed(&f.viewer, &ViewerWidget::annotationItemChanged);
    QSignalSpy deleted(&f.viewer, &ViewerWidget::annotationItemDeleteRequested);

    const QRectF bounds = annotation::AnnotationRenderer::itemBounds(f.viewer.transform().imageRect(),
                                                                     existing.items[0]);
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, bounds.center().toPoint());
    QVERIFY(f.viewer.isEditingText());
    QCOMPARE(f.viewer.textEditor()->toPlainText(), QStringLiteral("old note"));
    f.viewer.textEditor()->setPlainText(QStringLiteral("new note"));
    f.viewer.commitTextEdit();
    QCOMPARE(changed.count(), 1);
    const auto item = changed.at(0).at(1).value<AnnotationItem>();
    QCOMPARE(item.id, quint64(9));
    QCOMPARE(item.text, QStringLiteral("new note"));
    QCOMPARE(item.points, existing.items[0].points); // edited in place

    // Emptying the text deletes the label.
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, bounds.center().toPoint());
    f.viewer.textEditor()->clear();
    f.viewer.commitTextEdit();
    QCOMPARE(deleted.count(), 1);
    QCOMPARE(deleted.at(0).at(1).toULongLong(), quint64(9));
}

void TestAnnotationViewer::selectDragMovesAnItem()
{
    Fixture f;
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    AnnotationItem line = horizontalLine();
    line.id = 5;
    existing.items = {line};
    f.viewer.setAnnotationFrame(existing);
    useTool(f.viewer, AnnotationTool::Select);
    f.viewer.setAnnotateMode(true);
    QSignalSpy changed(&f.viewer, &ViewerWidget::annotationItemChanged);

    drag(f.viewer, f.at(0.5, 0.5), f.at(0.5, 0.7));
    QCOMPARE(f.viewer.selectedItemId(), quint64(5));
    QCOMPARE(changed.count(), 1);
    const auto moved = changed.at(0).at(1).value<AnnotationItem>();
    QCOMPARE(moved.id, quint64(5));
    for (int i = 0; i < moved.points.size(); ++i) {
        QVERIFY(std::abs(moved.points[i].y - 0.7f) < 0.02f);
        QVERIFY(std::abs(moved.points[i].x - line.points[i].x) < 0.01f);
    }

    // Dragging far off the picture stops at its edge.
    drag(f.viewer, f.at(0.5, 0.5), QPoint(f.viewer.width() - 1, f.at(0.5, 0.5).y()));
    const auto clamped = changed.last().at(1).value<AnnotationItem>();
    for (const auto& p : clamped.points) QVERIFY(p.x <= 1.0f);
    QVERIFY(std::abs(clamped.points[1].x - 1.0f) < 0.001f);

    // A click on empty picture clears the selection; a plain click selects without moving.
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.1));
    QCOMPARE(f.viewer.selectedItemId(), quint64(0));
    const int before = changed.count();
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.5));
    QCOMPARE(f.viewer.selectedItemId(), quint64(5));
    QCOMPARE(changed.count(), before);
}

void TestAnnotationViewer::deleteKeyRemovesTheSelection()
{
    Fixture f;
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    AnnotationItem line = horizontalLine();
    line.id = 5;
    existing.items = {line};
    f.viewer.setAnnotationFrame(existing);
    useTool(f.viewer, AnnotationTool::Select);
    f.viewer.setAnnotateMode(true);
    QSignalSpy deleted(&f.viewer, &ViewerWidget::annotationItemDeleteRequested);
    QTest::keyClick(&f.viewer, Qt::Key_Delete); // nothing selected
    QCOMPARE(deleted.count(), 0);
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.5));
    QTest::keyClick(&f.viewer, Qt::Key_Delete);
    QCOMPARE(deleted.count(), 1);
    QCOMPARE(deleted.at(0).at(1).toULongLong(), quint64(5));
}

void TestAnnotationViewer::doublePressOnTextWithSelectEdits()
{
    Fixture f;
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    existing.items = {textItem(9, QStringLiteral("note"))};
    f.viewer.setAnnotationFrame(existing);
    useTool(f.viewer, AnnotationTool::Select);
    f.viewer.setAnnotateMode(true);
    const QPoint on = annotation::AnnotationRenderer::itemBounds(f.viewer.transform().imageRect(),
                                                                 existing.items[0]).center().toPoint();
    // Mouse double-click.
    QTest::mouseDClick(&f.viewer, Qt::LeftButton, {}, on);
    QVERIFY(f.viewer.isEditingText());
    f.viewer.cancelTextEdit();

    // Two quick pen taps (tablets do not produce mouse double-clicks).
    auto pen = makeStylus(QPointingDevice::PointerType::Pen);
    QTest::qWait(QApplication::doubleClickInterval() + 50); // start a fresh double-press window
    sendTablet(&f.viewer, pen.get(), QEvent::TabletPress, on, 0.5);
    sendTablet(&f.viewer, pen.get(), QEvent::TabletRelease, on, 0.0);
    QVERIFY(!f.viewer.isEditingText());
    sendTablet(&f.viewer, pen.get(), QEvent::TabletPress, on, 0.5);
    sendTablet(&f.viewer, pen.get(), QEvent::TabletRelease, on, 0.0);
    QVERIFY(f.viewer.isEditingText());
}

void TestAnnotationViewer::changingFrameFinishesText()
{
    Fixture f;
    useTool(f.viewer, AnnotationTool::Text);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.4, 0.3));
    QTest::keyClicks(f.viewer.textEditor(), QStringLiteral("keep me"));
    f.viewer.setFrame(frame(13));
    QVERIFY(!f.viewer.isEditingText());
    QCOMPARE(drawn.count(), 1);
    QCOMPARE(drawn.at(0).at(0).toLongLong(), qint64(12)); // landed on the frame it was typed on
}

void TestAnnotationViewer::typedTextShowsAtItsFinalSize()
{
    // The application stylesheet sets a small pixel font on every widget, as
    // ATK Player's theme does; the editor must still show the real size.
    const QString previousStyle = qApp->styleSheet();
    qApp->setStyleSheet(QStringLiteral("QWidget { font-size: 12px; }"));
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Text;
    tool.sizePixels = 9;
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.2, 0.2));
    QTextEdit* editor = f.viewer.textEditor();
    QTest::keyClicks(editor, QStringLiteral("Big"));

    const QRectF target = f.viewer.transform().imageRect();
    auto expectedPx = [&](qreal sizePixels) {
        AnnotationItem probe;
        probe.size = sizePixels / 100.0; // 100 px tall source image
        return annotation::AnnotationRenderer::textFont(target, probe).pixelSize();
    };
    QVERIFY(expectedPx(9) > 40); // far from the 12 px the stylesheet asks for
    QCOMPARE(QFontInfo(editor->font()).pixelSize(), expectedPx(9));
    QCOMPARE(QFontInfo(editor->document()->defaultFont()).pixelSize(), expectedPx(9));

    // Changing the size while typing updates the text on screen immediately...
    tool.sizePixels = 5;
    f.viewer.setAnnotationTool(tool);
    QVERIFY(f.viewer.isEditingText());
    QCOMPARE(QFontInfo(editor->font()).pixelSize(), expectedPx(5));
    // ...and is what gets committed.
    f.viewer.commitTextEdit();
    QCOMPARE(drawn.count(), 1);
    QVERIFY(std::abs(drawn.at(0).at(1).value<AnnotationItem>().size - 0.05) < 1e-9);
    qApp->setStyleSheet(previousStyle);
}

void TestAnnotationViewer::textBoxResizesAndMovesWhileTyping()
{
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Text;
    tool.sizePixels = 2; // one line is shorter than the box, so the box sets the height
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    drag(f.viewer, f.at(0.2, 0.2), f.at(0.5, 0.4));
    QVERIFY(f.viewer.isEditingText());
    QTextEdit* editor = f.viewer.textEditor();
    const QRect before = editor->geometry();
    // The box keeps the height that was dragged out, not just one line.
    QVERIFY(std::abs(before.height() - (f.at(0.5, 0.4).y() - f.at(0.2, 0.2).y())) <= 4);

    // Bottom-right handle: just outside the text field's corner.
    const QPoint corner = before.bottomRight() + QPoint(7, 7);
    drag(f.viewer, corner, corner + QPoint(40, 30));
    QVERIFY(f.viewer.isEditingText()); // resizing does not finish the text
    QVERIFY(std::abs(editor->width() - (before.width() + 40)) <= 3);
    QVERIFY(std::abs(editor->height() - (before.height() + 30)) <= 3);

    // The border band moves the whole box.
    const QRect resized = editor->geometry();
    const QPoint border(resized.left() + resized.width() / 4, resized.top() - 4); // between handles
    drag(f.viewer, border, border + QPoint(-30, 20));
    QVERIFY(f.viewer.isEditingText());
    QVERIFY(std::abs(editor->x() - (resized.x() - 30)) <= 3);
    QVERIFY(std::abs(editor->y() - (resized.y() + 20)) <= 3);
    QCOMPARE(editor->size(), resized.size());

    QTest::keyClicks(editor, QStringLiteral("note"));
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(drawn.count(), 1);
    const auto item = drawn.at(0).at(1).value<AnnotationItem>();
    QVERIFY(annotation::isBoxText(item));
    const QRectF box = annotation::AnnotationRenderer::itemBounds(f.viewer.transform().imageRect(), item);
    QVERIFY(std::abs(box.width() - editor->width()) <= 4 || std::abs(box.width() - (resized.width() - 2)) <= 4);
    QVERIFY(std::abs(box.height() - (resized.height() - 2)) <= 4);
}

void TestAnnotationViewer::leavingAnnotateModeCancelsAStroke()
{
    Fixture f;
    f.viewer.setAnnotateMode(true);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.2, 0.5));
    QVERIFY(f.viewer.isDrawing());
    f.viewer.setAnnotateMode(false);
    QVERIFY(!f.viewer.isDrawing());
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.6, 0.5));
    QCOMPARE(drawn.count(), 0);
}

void TestAnnotationViewer::doubleClickDoesNotFitWhileAnnotating()
{
    Fixture f;
    f.viewer.zoomIn();
    QVERIFY(!f.viewer.transform().isFit());
    f.viewer.setAnnotateMode(true);
    QTest::mouseDClick(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.5));
    QVERIFY(!f.viewer.transform().isFit());
}

void TestAnnotationViewer::annotationsPaintOnlyOnTheirFrame()
{
    Fixture f;
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    AnnotationItem dot;
    dot.id = 1;
    dot.tool = AnnotationTool::Marker;
    dot.color = QColor(0, 255, 0);
    dot.size = 0.3;
    dot.points = {{0.5f, 0.5f, 1, 0, 0}};
    existing.items = {dot};
    f.viewer.setAnnotationFrame(existing);
    const QPoint centre = f.at(0.5, 0.5);
    QCOMPARE(f.viewer.grab().toImage().pixelColor(centre * f.viewer.devicePixelRatioF()).green(), 255);

    f.viewer.setFrame(frame(13)); // a different frame: its annotation must not show
    QCOMPARE(f.viewer.grab().toImage().pixelColor(centre * f.viewer.devicePixelRatioF()).green(), 0);

    f.viewer.setFrame(frame(12));
    f.viewer.setAnnotationsVisible(false);
    QCOMPARE(f.viewer.grab().toImage().pixelColor(centre * f.viewer.devicePixelRatioF()).green(), 0);
}

void TestAnnotationViewer::addedStrokesExtendTheCacheExactly()
{
    // A finished stroke is painted on top of the cached drawings instead of
    // redrawing them all; the result must match a full redraw, and any other
    // change (an edit, an undo) must still redraw from scratch.
    auto dot = [](quint64 id, qreal x, QColor color) {
        AnnotationItem item;
        item.id = id;
        item.tool = AnnotationTool::Pencil; // grainy: sensitive to where it is drawn
        item.color = color;
        item.size = 0.2;
        item.points = {{float(x), 0.5f, 1, 0, 0}, {float(x + 0.1), 0.6f, 1, 0, 0}};
        return item;
    };
    annotation::FrameAnnotation one;
    one.frame = 12;
    one.items = {dot(1, 0.2, Qt::red)};
    annotation::FrameAnnotation two = one;
    two.items.append(dot(2, 0.45, Qt::green));
    annotation::FrameAnnotation three = two;
    three.items.append(dot(3, 0.6, Qt::blue));

    Fixture extended;
    extended.viewer.setAnnotationFrame(one);
    extended.viewer.grab();
    extended.viewer.setAnnotationFrame(two);
    extended.viewer.setAnnotationFrame(three); // two additions before one repaint
    Fixture redrawn;
    redrawn.viewer.setAnnotationFrame(three);
    QCOMPARE(extended.viewer.grab().toImage(), redrawn.viewer.grab().toImage());

    // Editing an earlier stroke is not an addition: the old ink must go.
    annotation::FrameAnnotation edited = three;
    edited.items[0].color = Qt::white;
    extended.viewer.setAnnotationFrame(edited);
    redrawn.viewer.setAnnotationFrame(edited);
    QCOMPARE(extended.viewer.grab().toImage(), redrawn.viewer.grab().toImage());

    // Undo removes the newest stroke.
    edited.items.removeLast();
    extended.viewer.setAnnotationFrame(edited);
    redrawn.viewer.setAnnotationFrame(edited);
    QCOMPARE(extended.viewer.grab().toImage(), redrawn.viewer.grab().toImage());
    const QPoint gone = extended.at(0.65, 0.55) * extended.viewer.devicePixelRatioF();
    QCOMPARE(extended.viewer.grab().toImage().pixelColor(gone).blue(), 0);
}

void TestAnnotationViewer::tracingPaperWashesThePictureButNotTheDrawings()
{
    Fixture f; // black picture
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    AnnotationItem dot;
    dot.id = 1;
    dot.tool = AnnotationTool::Marker;
    dot.color = QColor(0, 0, 255);
    dot.size = 0.2;
    dot.opacity = 1.0;
    dot.points = {{0.7f, 0.5f, 1, 0, 0}};
    existing.items = {dot};
    f.viewer.setAnnotationFrame(existing);
    const qreal dpr = f.viewer.devicePixelRatioF();
    const QPoint picture = f.at(0.2, 0.5) * dpr;
    const QPoint ink = f.at(0.7, 0.5) * dpr;
    QCOMPARE(f.viewer.grab().toImage().pixelColor(picture).red(), 0);

    f.viewer.setTracingPaper(50);
    QImage shot = f.viewer.grab().toImage();
    QVERIFY(qAbs(shot.pixelColor(picture).red() - 128) <= 2); // half-white over black
    QCOMPARE(shot.pixelColor(ink).blue(), 255); // drawings sit on the paper, unfaded
    QVERIFY(shot.pixelColor(ink).red() < 60);

    f.viewer.setTracingPaper(100);
    QCOMPARE(f.viewer.grab().toImage().pixelColor(picture).red(), 255); // blank paper
    f.viewer.setTracingPaper(0);
    QCOMPARE(f.viewer.grab().toImage().pixelColor(picture).red(), 0);
}

void TestAnnotationViewer::ghostsPaintTintedAndFadedUnderTheFrame()
{
    Fixture f; // frame 12, black
    auto drawingAt = [](int64_t frameIndex, float x) {
        annotation::FrameAnnotation a;
        a.frame = frameIndex;
        AnnotationItem dot;
        dot.id = quint64(frameIndex + 1);
        dot.tool = AnnotationTool::Marker;
        dot.color = QColor(255, 255, 255);
        dot.size = 0.15;
        dot.opacity = 1.0;
        dot.points = {{x, 0.5f, 1, 0, 0}};
        a.items = {dot};
        return a;
    };
    annotation::SourceAnnotations set;
    set.setFrame(drawingAt(10, 0.2f)); // two frames earlier
    set.setFrame(drawingAt(11, 0.4f)); // one frame earlier
    set.setFrame(drawingAt(13, 0.8f)); // one frame later
    annotation::GhostSettings ghost;
    ghost.enabled = true;
    ghost.before = 2;
    ghost.after = 2;
    f.viewer.setGhostFrames(12, annotation::ghostFramesFor(set, 12, ghost));

    const qreal dpr = f.viewer.devicePixelRatioF();
    QImage shot = f.viewer.grab().toImage();
    const QColor far = shot.pixelColor(f.at(0.2, 0.5) * dpr);
    const QColor near = shot.pixelColor(f.at(0.4, 0.5) * dpr);
    const QColor later = shot.pixelColor(f.at(0.8, 0.5) * dpr);
    // Earlier frames ghost red, later green; nearer ghosts are stronger.
    QVERIFY2(near.red() > near.green() + 40, qPrintable(near.name()));
    QVERIFY2(later.green() > later.red() + 40, qPrintable(later.name()));
    QVERIFY2(near.red() > far.red() + 20, qPrintable(near.name() + far.name()));
    QVERIFY(near.red() < 230); // faded, never full strength
    QCOMPARE(shot.pixelColor(f.at(0.6, 0.5) * dpr).red(), 0); // nothing drawn there

    // Untinted ghosts keep their own colour; ghosts only show on their frame.
    ghost.tint = false;
    f.viewer.setGhostFrames(12, annotation::ghostFramesFor(set, 12, ghost));
    const QColor plain = f.viewer.grab().toImage().pixelColor(f.at(0.4, 0.5) * dpr);
    QVERIFY(qAbs(plain.red() - plain.green()) <= 3 && plain.red() > 60);
    f.viewer.setFrame(frame(20));
    QCOMPARE(f.viewer.grab().toImage().pixelColor(f.at(0.4, 0.5) * dpr).red(), 0);
    f.viewer.setFrame(frame(12));
    f.viewer.setGhostFrames(12, {});
    QCOMPARE(f.viewer.grab().toImage().pixelColor(f.at(0.4, 0.5) * dpr).red(), 0);
}

// Lifting the pen adds one stroke to the frame. The viewer must paint just
// that stroke onto its cache instead of re-rendering every earlier one (the
// cost that used to grow with each line), and the result must be exactly what
// a full re-render gives.
void TestAnnotationViewer::addingStrokesPaintsOnlyTheNewOne()
{
    Fixture f;
    annotation::AnnotationStore store;
    QObject::connect(&f.viewer, &ViewerWidget::annotationItemDrawn, &store,
                     [&](qint64 frame, const AnnotationItem& item) { store.addItem(frame, 0, item); });
    QObject::connect(&store, &annotation::AnnotationStore::frameChanged, &f.viewer,
                     [&](qint64 frame) { f.viewer.setAnnotationFrame(store.frame(frame)); });
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Pencil;
    tool.sizePixels = 6;
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);

    auto stroke = [&](qreal y) {
        QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.2, y));
        for (int i = 1; i <= 12; ++i) {
            const QPoint p = f.at(0.2 + 0.05 * i, y + 0.04 * std::sin(i));
            QMouseEvent move(QEvent::MouseMove, p, f.viewer.mapToGlobal(p), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&f.viewer, &move);
        }
        QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.8, y));
        f.viewer.grab(); // paint, so the cache catches up
    };
    stroke(0.2);
    const quint64 rebuilds = f.viewer.annotationCacheRebuilds();
    for (qreal y : {0.35, 0.5, 0.65, 0.8}) stroke(y);
    QCOMPARE(store.frame(12).items.size(), 5);
    QCOMPARE(f.viewer.annotationCacheRebuilds(), rebuilds);

    // A note changes the frame but not its drawings: still no re-render.
    annotation::FrameNote note;
    note.text = QStringLiteral("timing");
    store.addNote(12, 0, note);
    f.viewer.grab();
    QCOMPARE(f.viewer.annotationCacheRebuilds(), rebuilds);

    const QImage incremental = f.viewer.grab().toImage();
    // Force a full re-render of the same drawings and compare every pixel.
    f.viewer.setAnnotationFrame({});
    f.viewer.setAnnotationFrame(store.frame(12));
    const QImage full = f.viewer.grab().toImage();
    QVERIFY(f.viewer.annotationCacheRebuilds() > rebuilds);
    QCOMPARE(incremental, full);

    // Undo is not an append: it must re-render, and show one stroke fewer.
    store.undo(); // the note
    store.undo(); // the last stroke
    QCOMPARE(store.frame(12).items.size(), 4);
    const quint64 beforeUndoPaint = f.viewer.annotationCacheRebuilds();
    QVERIFY(f.viewer.grab().toImage() != full);
    QCOMPARE(f.viewer.annotationCacheRebuilds(), beforeUndoPaint + 1);
}

QTEST_MAIN(TestAnnotationViewer)
#include "tst_annotationviewer.moc"
