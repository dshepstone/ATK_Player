#include "annotation/AnnotationTypes.h"
#include "media/VideoFrame.h"
#include "ui/ViewerWidget.h"

#include <QApplication>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTest>

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
    void flippedViewStoresUnflippedCoordinates();
    void zoomedViewMapsToTheSameImagePoint();
    void shapeNeedsADrag();
    void textToolAsksForText();
    void leavingAnnotateModeCancelsAStroke();
    void doubleClickDoesNotFitWhileAnnotating();
    void annotationsPaintOnlyOnTheirFrame();
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
    f.viewer.setAnnotateMode(true);
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    AnnotationItem line;
    line.id = 77;
    line.tool = AnnotationTool::Pen;
    line.size = 0.05;
    line.points = {{0.2f, 0.5f, 1, 0, 0}, {0.8f, 0.5f, 1, 0, 0}};
    existing.items = {line};
    f.viewer.setAnnotationFrame(existing);

    auto eraser = makeStylus(QPointingDevice::PointerType::Eraser);
    QSignalSpy drawn(&f.viewer, &ViewerWidget::annotationItemDrawn);
    QSignalSpy erased(&f.viewer, &ViewerWidget::annotationEraseRequested);
    QVERIFY(sendTablet(&f.viewer, eraser.get(), QEvent::TabletPress, f.at(0.1, 0.2), 1.0)); // misses
    QCOMPARE(erased.count(), 0);
    QVERIFY(sendTablet(&f.viewer, eraser.get(), QEvent::TabletMove, f.at(0.5, 0.5), 1.0));  // crosses the line
    QVERIFY(sendTablet(&f.viewer, eraser.get(), QEvent::TabletRelease, f.at(0.5, 0.5), 0.0));
    QVERIFY(erased.count() >= 1);
    QCOMPARE(erased.at(0).at(0).toLongLong(), qint64(12));
    QCOMPARE(erased.at(0).at(1).value<QVector<quint64>>(), QVector<quint64>({77}));
    QCOMPARE(drawn.count(), 0);
}

void TestAnnotationViewer::fastEraserDragCannotJumpALine()
{
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Eraser;
    tool.sizePixels = 2; // a small eraser
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    annotation::FrameAnnotation existing;
    existing.frame = 12;
    AnnotationItem line;
    line.id = 5;
    line.tool = AnnotationTool::Pen;
    line.size = 0.01;
    line.points = {{0.1f, 0.5f, 1, 0, 0}, {0.9f, 0.5f, 1, 0, 0}};
    existing.items = {line};
    f.viewer.setAnnotationFrame(existing);
    QSignalSpy erased(&f.viewer, &ViewerWidget::annotationEraseRequested);
    // One big jump from above the line to below it, with no sample on it.
    QTest::mousePress(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.1));
    QMouseEvent move(QEvent::MouseMove, f.at(0.5, 0.9), f.viewer.mapToGlobal(f.at(0.5, 0.9)), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&f.viewer, &move);
    QTest::mouseRelease(&f.viewer, Qt::LeftButton, {}, f.at(0.5, 0.9));
    QCOMPARE(erased.count(), 1);
    QCOMPARE(erased.at(0).at(1).value<QVector<quint64>>(), QVector<quint64>({5}));
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

void TestAnnotationViewer::textToolAsksForText()
{
    Fixture f;
    annotation::AnnotationToolSettings tool;
    tool.tool = AnnotationTool::Text;
    f.viewer.setAnnotationTool(tool);
    f.viewer.setAnnotateMode(true);
    QSignalSpy asked(&f.viewer, &ViewerWidget::annotationTextRequested);
    QTest::mouseClick(&f.viewer, Qt::LeftButton, {}, f.at(0.4, 0.3));
    QCOMPARE(asked.count(), 1);
    QCOMPARE(asked.at(0).at(0).toLongLong(), qint64(12));
    const QPointF n = asked.at(0).at(1).toPointF();
    QVERIFY(std::abs(n.x() - 0.4) < 0.02 && std::abs(n.y() - 0.3) < 0.02);
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

QTEST_MAIN(TestAnnotationViewer)
#include "tst_annotationviewer.moc"
