#include "annotation/AnnotationRenderer.h"
#include "annotation/AnnotationTransform.h"
#include "annotation/AnnotationTypes.h"

#include <QImage>
#include <QPainter>
#include <QTest>
#include <QtMath>

#include <cmath>
#include <limits>

using namespace atk::annotation;

namespace {
bool near(const QPointF& a, const QPointF& b, qreal tolerance = 1e-6)
{
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

bool near(const QSizeF& a, const QSizeF& b, qreal tolerance = 1e-6)
{
    return std::abs(a.width() - b.width()) <= tolerance && std::abs(a.height() - b.height()) <= tolerance;
}

TransformBox box(QPointF center, QSizeF size, qreal angle = 0.0)
{
    TransformBox result;
    result.center = center;
    result.size = size;
    result.angle = angle;
    return result;
}

AnnotationItem shape(AnnotationTool tool, QPointF a, QPointF b, qreal rotation = 0.0)
{
    AnnotationItem item;
    item.tool = tool;
    item.color = QColor(255, 0, 0);
    item.size = 0.02;
    item.rotation = rotation;
    item.points = {{float(a.x()), float(a.y()), 1, 0, 0}, {float(b.x()), float(b.y()), 1, 0, 0}};
    return item;
}

QPointF pictureOf(const StrokePoint& p, const QSizeF& size) { return {p.x * size.width(), p.y * size.height()}; }

bool painted(const QImage& image, int x, int y) { return qAlpha(image.pixel(x, y)) > 100; }
} // namespace

class TestAnnotationTransform : public QObject {
    Q_OBJECT
private slots:
    void cornerAndEdgeHandlesMoveOnlyTheirSides();
    void shiftKeepsProportionsAndAltScalesFromTheCentre();
    void boxNeverCollapsesOrTurnsInsideOut();
    void rotatedBoxScalesAlongItsOwnSides();
    void rotateHandleTurnsAndSnaps();
    void handlesOfferedPerKind();
    void strokesScaleAndRotateRigidlyOnANonSquarePicture();
    void shapesKeepTheirOutlineAndStoreTheTurn();
    void textScalesItsTypeWithTheBox();
    void pointsStayOnThePicture();
    void lineEndsMoveIndependently();
    void rotationIsValidatedPerKind();
    void rotatedShapesRenderHitTestAndBoundTurned();
};

void TestAnnotationTransform::cornerAndEdgeHandlesMoveOnlyTheirSides()
{
    const TransformBox start = box({100, 100}, {80, 40});
    QVERIFY(near(start.handlePoint(0), {60, 80}));
    QVERIFY(near(start.handlePoint(4), {140, 120}));

    // Bottom-right corner: right and bottom follow, top-left stays put.
    const TransformBox corner = draggedBox(start, 4, {140, 120}, {160, 140}, {});
    QVERIFY(near(corner.size, {100, 60}));
    QVERIFY(near(corner.handlePoint(0), {60, 80}));

    // Left edge: only the width changes.
    const TransformBox edge = draggedBox(start, 7, {60, 100}, {40, 130}, {});
    QVERIFY(near(edge.size, {100, 40}));
    QVERIFY(near(edge.handlePoint(4), {140, 120}));
}

void TestAnnotationTransform::shiftKeepsProportionsAndAltScalesFromTheCentre()
{
    const TransformBox start = box({100, 100}, {80, 40});
    TransformOptions keep;
    keep.keepAspect = true;
    const TransformBox proportional = draggedBox(start, 4, {140, 120}, {160, 120}, keep);
    QVERIFY(near(proportional.size, {100, 50})); // the further-moved side leads
    QVERIFY(near(proportional.handlePoint(0), {60, 80}));

    TransformOptions centred;
    centred.fromCenter = true;
    const TransformBox wider = draggedBox(start, 3, {140, 100}, {150, 100}, centred);
    QVERIFY(near(wider.size, {100, 40}));
    QVERIFY(near(wider.center, {100, 100}));
}

void TestAnnotationTransform::boxNeverCollapsesOrTurnsInsideOut()
{
    const TransformBox start = box({100, 100}, {80, 40});
    TransformOptions options;
    options.minimumSize = 6.0;
    const TransformBox crushed = draggedBox(start, 4, {140, 120}, {0, 0}, options);
    QVERIFY(near(crushed.size, {6, 6}));
    QVERIFY(near(crushed.handlePoint(0), {60, 80}));

    // An axis already thinner than the minimum is left alone, not inflated.
    const TransformBox thin = box({100, 100}, {80, 2});
    const TransformBox wider = draggedBox(thin, 3, {140, 100}, {160, 100}, options);
    QVERIFY(near(wider.size, {100, 2}));
}

void TestAnnotationTransform::rotatedBoxScalesAlongItsOwnSides()
{
    // Turned a quarter clockwise: the box's own right side now faces down.
    const TransformBox start = box({100, 100}, {80, 40}, 90.0);
    QVERIFY(near(start.handlePoint(3), {100, 140}, 1e-9));
    const TransformBox dragged = draggedBox(start, 3, {100, 140}, {100, 160}, {});
    QVERIFY(near(dragged.size, {100, 40}, 1e-9));
    QVERIFY(near(dragged.center, {100, 110}, 1e-9));
    QCOMPARE(dragged.angle, 90.0);
    QVERIFY(near(dragged.handlePoint(7), start.handlePoint(7), 1e-9)); // opposite side fixed
}

void TestAnnotationTransform::rotateHandleTurnsAndSnaps()
{
    const TransformBox start = box({100, 100}, {80, 40});
    const TransformBox quarter = draggedBox(start, kRotateHandle, {100, 50}, {150, 100}, {});
    QVERIFY(std::abs(quarter.angle - 90.0) < 1e-9);
    QVERIFY(near(quarter.center, start.center));
    QVERIFY(near(quarter.size, start.size));

    // 38 degrees snaps to 45 with Shift; the angle stays within (-180, 180].
    const qreal r = qDegreesToRadians(-90.0 + 38.0);
    const QPointF at = QPointF(100, 100) + 50.0 * QPointF(std::cos(r), std::sin(r));
    TransformOptions snap;
    snap.snapAngle = true;
    QVERIFY(std::abs(draggedBox(start, kRotateHandle, {100, 50}, at, snap).angle - 45.0) < 1e-9);
    const TransformBox turned = box({100, 100}, {80, 40}, 170.0);
    const TransformBox past = draggedBox(turned, kRotateHandle, {100, 50}, {150, 100}, {});
    QVERIFY(std::abs(past.angle - (-100.0)) < 1e-9);
}

void TestAnnotationTransform::handlesOfferedPerKind()
{
    const AnnotationItem rect = shape(AnnotationTool::Rectangle, {0.1, 0.1}, {0.3, 0.3});
    QVERIFY(hasBoxHandles(rect) && hasEdgeHandles(rect) && canRotate(rect) && !hasEndPointHandles(rect));
    const AnnotationItem line = shape(AnnotationTool::Arrow, {0.1, 0.1}, {0.3, 0.3});
    QVERIFY(!hasBoxHandles(line) && !canRotate(line) && hasEndPointHandles(line));
    AnnotationItem text;
    text.tool = AnnotationTool::Text;
    text.text = QStringLiteral("hi");
    text.points = {{0.2f, 0.2f, 1, 0, 0}};
    QVERIFY(hasBoxHandles(text) && !hasEdgeHandles(text) && !canRotate(text));
    AnnotationItem dab;
    dab.tool = AnnotationTool::Pen;
    dab.points = {{0.2f, 0.2f, 1, 0, 0}};
    QVERIFY(!hasBoxHandles(dab) && !canRotate(dab) && !hasEndPointHandles(dab));
    dab.points.append({0.3f, 0.2f, 1, 0, 0});
    QVERIFY(hasBoxHandles(dab) && canRotate(dab));
}

void TestAnnotationTransform::strokesScaleAndRotateRigidlyOnANonSquarePicture()
{
    const QSizeF picture(400, 200);
    AnnotationItem stroke;
    stroke.tool = AnnotationTool::Pen;
    stroke.size = 0.01;
    stroke.points = {{0.4f, 0.5f, 0.3f, 0, 0}, {0.6f, 0.5f, 0.9f, 0, 0}};
    const TransformBox from = transformBox(stroke, picture);
    // Points' bounds, padded by the brush radius (1 px here).
    QVERIFY(near(from.center, {200, 100}, 1e-4));
    QVERIFY(near(from.size, {82, 2}, 1e-4));

    // A quarter turn keeps the stroke 80 px long on screen, although that is
    // 0.2 of the width but 0.4 of the height.
    TransformBox turned = from;
    turned.angle = 90.0;
    const AnnotationItem rotated = transformedItem(stroke, picture, from, turned);
    QVERIFY(near(pictureOf(rotated.points[0], picture), {200, 60}, 1e-3));
    QVERIFY(near(pictureOf(rotated.points[1], picture), {200, 140}, 1e-3));
    QCOMPARE(rotated.points[1].pressure, 0.9f); // everything but position is kept
    QCOMPARE(rotated.size, stroke.size);
    QCOMPARE(rotated.rotation, 0.0); // baked into the points

    TransformBox doubled = from;
    doubled.size = QSizeF(164, 2);
    const AnnotationItem wider = transformedItem(stroke, picture, from, doubled);
    QVERIFY(near(pictureOf(wider.points[0], picture), {120, 100}, 1e-3));
    QVERIFY(near(pictureOf(wider.points[1], picture), {280, 100}, 1e-3));
    QVERIFY(isValidItem(wider));
}

void TestAnnotationTransform::shapesKeepTheirOutlineAndStoreTheTurn()
{
    const QSizeF picture(400, 200);
    const AnnotationItem rect = shape(AnnotationTool::Rectangle, {0.25, 0.25}, {0.75, 0.75});
    const TransformBox from = transformBox(rect, picture);
    QVERIFY(near(from.center, {200, 100}, 1e-4));
    QVERIFY(near(from.size, {200, 100}, 1e-4)); // the outline itself, no padding

    TransformBox turned = from;
    turned.angle = 30.0;
    const AnnotationItem rotated = transformedItem(rect, picture, from, turned);
    QCOMPARE(rotated.rotation, 30.0);
    QCOMPARE(rotated.points, rect.points);
    QVERIFY(isValidItem(rotated));
    QVERIFY(near(transformBox(rotated, picture).handlePoint(0), turned.handlePoint(0), 1e-3));

    TransformBox smaller = from;
    smaller.size = QSizeF(100, 50);
    const AnnotationItem scaled = transformedItem(rect, picture, from, smaller);
    QVERIFY(std::abs(scaled.points[0].x - 0.375f) < 1e-6f && std::abs(scaled.points[0].y - 0.375f) < 1e-6f);
    QVERIFY(std::abs(scaled.points[1].x - 0.625f) < 1e-6f && std::abs(scaled.points[1].y - 0.625f) < 1e-6f);

    // Turning back to upright stores exactly zero.
    const AnnotationItem upright = transformedItem(rotated, picture, turned, from);
    QCOMPARE(upright.rotation, 0.0);
}

void TestAnnotationTransform::textScalesItsTypeWithTheBox()
{
    const QSizeF picture(400, 200);
    AnnotationItem text;
    text.tool = AnnotationTool::Text;
    text.size = 0.03;
    text.text = QStringLiteral("Fix the arc");
    text.points = {{0.2f, 0.2f, 1, 0, 0}};
    const TransformBox from = transformBox(text, picture);
    QVERIFY(near(from.handlePoint(0), {80, 40}, 1e-3)); // the anchor is the top-left
    TransformOptions keep;
    keep.keepAspect = true;
    const TransformBox to = draggedBox(from, 4, from.handlePoint(4), from.handlePoint(4) + QPointF(from.size.width(), 0), keep);
    const AnnotationItem bigger = transformedItem(text, picture, from, to);
    QVERIFY(std::abs(bigger.size - 0.06) < 1e-9);
    QCOMPARE(bigger.points[0], text.points[0]);
    QCOMPARE(bigger.rotation, 0.0);
    QVERIFY(isValidItem(bigger));
}

void TestAnnotationTransform::pointsStayOnThePicture()
{
    const QSizeF picture(200, 200);
    AnnotationItem stroke;
    stroke.tool = AnnotationTool::Marker;
    stroke.size = 0.02;
    stroke.points = {{0.1f, 0.1f, 1, 0, 0}, {0.9f, 0.9f, 1, 0, 0}};
    const TransformBox from = transformBox(stroke, picture);
    TransformBox huge = from;
    huge.size = from.size * 3.0;
    huge.angle = 45.0;
    const AnnotationItem result = transformedItem(stroke, picture, from, huge);
    QVERIFY(isValidItem(result));
    for (const StrokePoint& p : result.points) QVERIFY(p.x >= 0.0f && p.x <= 1.0f && p.y >= 0.0f && p.y <= 1.0f);
}

void TestAnnotationTransform::lineEndsMoveIndependently()
{
    const AnnotationItem line = shape(AnnotationTool::Line, {0.1, 0.1}, {0.5, 0.5});
    const AnnotationItem moved = movedEndPoint(line, kEndPointHandle, {0.9, 1.4});
    QCOMPARE(moved.points[0], line.points[0]);
    QCOMPARE(moved.points[1].x, 0.9f);
    QCOMPARE(moved.points[1].y, 1.0f); // kept on the picture
    QCOMPARE(movedEndPoint(line, kStartPointHandle, {0.3, 0.2}).points[0].x, 0.3f);
    QCOMPARE(movedEndPoint(line, kRotateHandle, {0.3, 0.2}), line); // not an end
}

void TestAnnotationTransform::rotationIsValidatedPerKind()
{
    AnnotationItem rect = shape(AnnotationTool::Ellipse, {0.1, 0.1}, {0.3, 0.3}, 180.0);
    QVERIFY(isValidItem(rect));
    rect.rotation = -180.0; // the same turn as 180, stored one way only
    QVERIFY(!isValidItem(rect));
    rect.rotation = std::numeric_limits<qreal>::quiet_NaN();
    QVERIFY(!isValidItem(rect));
    QVERIFY(!isValidItem(shape(AnnotationTool::Line, {0.1, 0.1}, {0.3, 0.3}, 10.0)));
}

void TestAnnotationTransform::rotatedShapesRenderHitTestAndBoundTurned()
{
    // A flat 80 x 20 rectangle centred at (100, 100), turned to stand upright.
    const QRectF rect(0, 0, 200, 200);
    const AnnotationItem flat = shape(AnnotationTool::Rectangle, {0.3, 0.45}, {0.7, 0.55});
    AnnotationItem upright = flat;
    upright.rotation = 90.0;

    auto render = [&](const AnnotationItem& item) {
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        AnnotationRenderer::paintItem(painter, rect, item);
        return image;
    };
    const QImage before = render(flat);
    const QImage after = render(upright);
    QVERIFY(painted(before, 60, 100) && !painted(before, 100, 60));
    QVERIFY(painted(after, 100, 60) && !painted(after, 60, 100));

    QVERIFY(AnnotationRenderer::hitTest(rect, upright, {100, 60}, 1.0));
    QVERIFY(!AnnotationRenderer::hitTest(rect, upright, {60, 100}, 1.0));
    const QRectF bounds = AnnotationRenderer::itemBounds(rect, upright);
    QVERIFY(bounds.contains(QPointF(100, 59)) && bounds.width() < 40.0);
}

QTEST_MAIN(TestAnnotationTransform)
#include "tst_annotationtransform.moc"
