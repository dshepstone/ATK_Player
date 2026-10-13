#include "annotation/AnnotationRenderer.h"
#include "annotation/BrushTip.h"
#include "annotation/StrokeBuilder.h"

#include <QImage>
#include <QPainter>
#include <QTest>

using namespace atk::annotation;

namespace {
QImage blank(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

FrameAnnotation frameWith(const AnnotationItem& item)
{
    FrameAnnotation frame;
    frame.items = {item};
    return frame;
}

AnnotationItem horizontal(AnnotationTool tool, float y, float pressureStart, float pressureEnd, int samples = 40)
{
    AnnotationItem item;
    item.tool = tool;
    item.color = QColor(255, 0, 0);
    item.size = 0.08;
    for (int i = 0; i < samples; ++i) {
        const float t = float(i) / float(samples - 1);
        item.points.append({0.1f + 0.8f * t, y, pressureStart + (pressureEnd - pressureStart) * t, 0, 0});
    }
    return item;
}

/// Number of painted pixels in column x.
int columnCoverage(const QImage& image, int x)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        if (qAlpha(image.pixel(x, y)) > 40) ++count;
    return count;
}
} // namespace

class TestAnnotationRenderer : public QObject {
    Q_OBJECT
private slots:
    void partialRepaintsMatchAFullRender_data();
    void partialRepaintsMatchAFullRender();
    void pressureWidensPen();
    void markerIsConstantWidthAndNeverDarkensWhereItCrossesItself();
    void sizeScalesWithImageResolution();
    void pencilGrainIsDeterministicPerSeed();
    void grainStaysInRange();
    void opacityAppliesOnce();
    void shapesLandWhereDrawn();
    void textRendersInsideItsBounds();
    void textLinesAndBoxWrapping();
    void hitTestPerKind();
    void invalidItemsAndEmptyFramesPaintNothing();
    void flippedPainterMirrorsAnnotation();
    void singlePointStrokeDrawsADot();
    void stampRepeatsTheTipAtItsSpacing();
    void stampFollowsPressureAndColour();
    void stampWithoutItsTipFallsBackToARoundBrush();

    void pressureCurveShapesAndFloors();
    void builderClampsDecimatesAndFinishes();
    void stabilizerSmoothsJitter();
    void pressureCanBeDisabled();
};

void TestAnnotationRenderer::pressureWidensPen()
{
    QImage image = blank(400, 200);
    AnnotationRenderer::renderOnto(image, frameWith(horizontal(AnnotationTool::Pen, 0.5f, 0.1f, 1.0f)));
    const int light = columnCoverage(image, 70);   // near the light start
    const int heavy = columnCoverage(image, 330);  // near the heavy end
    QVERIFY2(heavy > light * 2, qPrintable(QStringLiteral("light %1 heavy %2").arg(light).arg(heavy)));
    QVERIFY(light > 0);
}

void TestAnnotationRenderer::markerIsConstantWidthAndNeverDarkensWhereItCrossesItself()
{
    QImage image = blank(400, 400);
    AnnotationItem marker;
    marker.tool = AnnotationTool::Marker;
    marker.color = QColor(0, 0, 255);
    marker.opacity = 0.5;
    marker.size = 0.05;
    // An X drawn as one stroke: down-right, then back up across itself.
    marker.points = {{0.2f, 0.2f, 0.1f, 0, 0}, {0.5f, 0.5f, 0.1f, 0, 0}, {0.8f, 0.8f, 1.0f, 0, 0},
                     {0.8f, 0.2f, 1.0f, 0, 0}, {0.5f, 0.5f, 1.0f, 0, 0}, {0.2f, 0.8f, 0.5f, 0, 0}};
    AnnotationRenderer::renderOnto(image, frameWith(marker));
    const int crossing = qAlpha(image.pixel(200, 200));
    const int single = qAlpha(image.pixel(100, 100));
    QVERIFY(single > 100 && single < 160); // ~50% opacity
    QVERIFY2(std::abs(crossing - single) <= 3,
             qPrintable(QStringLiteral("crossing %1 single %2").arg(crossing).arg(single)));

    // Pressure must not change marker width.
    QImage widths = blank(400, 200);
    AnnotationItem flat = horizontal(AnnotationTool::Marker, 0.5f, 0.1f, 1.0f);
    AnnotationRenderer::renderOnto(widths, frameWith(flat));
    QVERIFY(std::abs(columnCoverage(widths, 70) - columnCoverage(widths, 330)) <= 1);
}

void TestAnnotationRenderer::sizeScalesWithImageResolution()
{
    const AnnotationItem item = horizontal(AnnotationTool::Pen, 0.5f, 1.0f, 1.0f);
    QImage small = blank(320, 180);
    QImage large = blank(640, 360);
    AnnotationRenderer::renderOnto(small, frameWith(item));
    AnnotationRenderer::renderOnto(large, frameWith(item));
    const int s = columnCoverage(small, 160);
    const int l = columnCoverage(large, 320);
    QVERIFY(s > 0);
    QVERIFY2(std::abs(l - 2 * s) <= 2, qPrintable(QStringLiteral("small %1 large %2").arg(s).arg(l)));
    // Normalised position: centred vertically at both sizes.
    QVERIFY(qAlpha(small.pixel(160, 90)) > 200);
    QVERIFY(qAlpha(large.pixel(320, 180)) > 200);
    QCOMPARE(qAlpha(small.pixel(160, 10)), 0);
}

void TestAnnotationRenderer::pencilGrainIsDeterministicPerSeed()
{
    AnnotationItem pencil = horizontal(AnnotationTool::Pencil, 0.5f, 1.0f, 1.0f);
    pencil.textureSeed = 7;
    QImage a = blank(300, 150), b = blank(300, 150), c = blank(300, 150);
    AnnotationRenderer::renderOnto(a, frameWith(pencil));
    AnnotationRenderer::renderOnto(b, frameWith(pencil));
    QCOMPARE(a, b);
    pencil.textureSeed = 8;
    AnnotationRenderer::renderOnto(c, frameWith(pencil));
    QVERIFY(a != c);

    // Grain makes the pencil visibly uneven compared with the pen.
    int distinct = 0;
    QSet<int> alphas;
    for (int x = 100; x < 200; ++x) alphas.insert(qAlpha(a.pixel(x, 75)));
    distinct = int(alphas.size());
    QVERIFY2(distinct > 5, qPrintable(QString::number(distinct)));
}

void TestAnnotationRenderer::grainStaysInRange()
{
    for (int i = 0; i < 5000; ++i) {
        const float g = AnnotationRenderer::grain(i * 7, i * 13, quint32(i));
        QVERIFY(g >= 0.0f && g < 1.0f);
    }
}

void TestAnnotationRenderer::opacityAppliesOnce()
{
    AnnotationItem pen = horizontal(AnnotationTool::Pen, 0.5f, 1.0f, 1.0f);
    pen.opacity = 0.25;
    QImage image = blank(300, 150);
    AnnotationRenderer::renderOnto(image, frameWith(pen));
    const int alpha = qAlpha(image.pixel(150, 75));
    QVERIFY2(alpha > 55 && alpha < 75, qPrintable(QString::number(alpha)));
}

void TestAnnotationRenderer::shapesLandWhereDrawn()
{
    AnnotationItem rect;
    rect.tool = AnnotationTool::Rectangle;
    rect.color = QColor(0, 255, 0);
    rect.size = 0.02;
    rect.points = {{0.25f, 0.25f, 1, 0, 0}, {0.75f, 0.75f, 1, 0, 0}};
    QImage image = blank(200, 200);
    AnnotationRenderer::renderOnto(image, frameWith(rect));
    QVERIFY(qAlpha(image.pixel(50, 100)) > 200);   // left edge
    QVERIFY(qAlpha(image.pixel(150, 100)) > 200);  // right edge
    QCOMPARE(qAlpha(image.pixel(100, 100)), 0);    // hollow
    QCOMPARE(qAlpha(image.pixel(10, 10)), 0);

    AnnotationItem arrow = rect;
    arrow.tool = AnnotationTool::Arrow;
    arrow.points = {{0.1f, 0.5f, 1, 0, 0}, {0.9f, 0.5f, 1, 0, 0}};
    QImage arrowImage = blank(200, 200);
    AnnotationRenderer::renderOnto(arrowImage, frameWith(arrow));
    QVERIFY(qAlpha(arrowImage.pixel(100, 100)) > 200);
    // Arrow head barbs extend above and below the tip.
    int above = 0;
    for (int y = 80; y < 97; ++y) above += qAlpha(arrowImage.pixel(170, y)) > 100;
    QVERIFY(above > 0);

    AnnotationItem ellipse = rect;
    ellipse.tool = AnnotationTool::Ellipse;
    QImage ellipseImage = blank(200, 200);
    AnnotationRenderer::renderOnto(ellipseImage, frameWith(ellipse));
    QVERIFY(qAlpha(ellipseImage.pixel(100, 50)) > 200);  // top of ellipse
    QCOMPARE(qAlpha(ellipseImage.pixel(55, 55)), 0);     // rect corner is outside the ellipse
}

void TestAnnotationRenderer::textRendersInsideItsBounds()
{
    AnnotationItem text;
    text.tool = AnnotationTool::Text;
    text.color = Qt::yellow;
    text.size = 0.02;
    text.text = QStringLiteral("fix the nose");
    text.points = {{0.1f, 0.4f, 1, 0, 0}};
    QImage image = blank(400, 200);
    AnnotationRenderer::renderOnto(image, frameWith(text));
    const QRect bounds = AnnotationRenderer::itemBounds(QRectF(0, 0, 400, 200), text).toAlignedRect();
    int inside = 0, outside = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 400; ++x)
            if (qAlpha(image.pixel(x, y)) > 0) (bounds.adjusted(-3, -3, 3, 3).contains(x, y) ? inside : outside)++;
    QVERIFY(inside > 50);
    QCOMPARE(outside, 0);
}

void TestAnnotationRenderer::textLinesAndBoxWrapping()
{
    const QRectF rect(0, 0, 800, 400);
    AnnotationItem text;
    text.tool = AnnotationTool::Text;
    text.size = 0.01;
    text.points = {{0.1f, 0.1f, 1, 0, 0}};
    text.text = QStringLiteral("first line\nsecond line");
    const auto pointBlock = AnnotationRenderer::layoutText(rect, text);
    QCOMPARE(pointBlock.lines.size(), 2);
    QCOMPARE(pointBlock.lines[0].text, QStringLiteral("first line"));
    QCOMPARE(pointBlock.lines[1].text, QStringLiteral("second line"));
    QVERIFY(pointBlock.lines[1].baseline.y() > pointBlock.lines[0].baseline.y());

    // A long single line wraps to the box width when the text has a box.
    text.text = QStringLiteral("this sentence is far too long to fit on one line of a narrow box");
    const auto unwrapped = AnnotationRenderer::layoutText(rect, text);
    QCOMPARE(unwrapped.lines.size(), 1);
    text.points.append({0.35f, 0.5f, 1, 0, 0}); // a 200 px wide box
    const auto wrapped = AnnotationRenderer::layoutText(rect, text);
    QVERIFY(wrapped.lines.size() > 2);
    QVERIFY(std::abs(wrapped.bounds.width() - 200.0) < 0.5);
    for (const auto& line : wrapped.lines) QVERIFY(line.width <= 200.5);

    // Painted text stays inside the laid-out bounds.
    QImage image = blank(800, 400);
    AnnotationRenderer::renderOnto(image, frameWith(text));
    const QRect allowed = wrapped.bounds.toAlignedRect().adjusted(-4, -4, 4, 4);
    for (int y = 0; y < 400; ++y)
        for (int x = 0; x < 800; ++x)
            if (qAlpha(image.pixel(x, y)) > 0) QVERIFY2(allowed.contains(x, y), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
}

void TestAnnotationRenderer::hitTestPerKind()
{
    const QRectF r(0, 0, 400, 200);
    const AnnotationItem pen = horizontal(AnnotationTool::Pen, 0.5f, 1.0f, 1.0f);
    QVERIFY(AnnotationRenderer::hitTest(r, pen, QPointF(200, 100), 2));
    QVERIFY(AnnotationRenderer::hitTest(r, pen, QPointF(200, 100 + 7), 2)); // within radius 8
    QVERIFY(!AnnotationRenderer::hitTest(r, pen, QPointF(200, 150), 2));

    AnnotationItem rect;
    rect.tool = AnnotationTool::Rectangle;
    rect.size = 0.01;
    rect.points = {{0.25f, 0.25f, 1, 0, 0}, {0.75f, 0.75f, 1, 0, 0}};
    QVERIFY(AnnotationRenderer::hitTest(r, rect, QPointF(100, 100), 2));   // left edge x=100
    QVERIFY(!AnnotationRenderer::hitTest(r, rect, QPointF(200, 100), 2));  // hollow centre

    AnnotationItem text;
    text.tool = AnnotationTool::Text;
    text.size = 0.02;
    text.text = QStringLiteral("note");
    text.points = {{0.1f, 0.1f, 1, 0, 0}};
    const QRectF tb = AnnotationRenderer::itemBounds(r, text);
    QVERIFY(AnnotationRenderer::hitTest(r, text, tb.center(), 0));
    QVERIFY(!AnnotationRenderer::hitTest(r, text, QPointF(390, 190), 0));
}

void TestAnnotationRenderer::invalidItemsAndEmptyFramesPaintNothing()
{
    QImage image = blank(100, 100);
    const QImage before = image;
    AnnotationRenderer::renderOnto(image, FrameAnnotation{});
    QCOMPARE(image, before);
    AnnotationItem bad = horizontal(AnnotationTool::Pen, 0.5f, 1, 1);
    bad.size = 9.0; // invalid
    AnnotationRenderer::renderOnto(image, frameWith(bad));
    QCOMPARE(image, before);
}

void TestAnnotationRenderer::flippedPainterMirrorsAnnotation()
{
    AnnotationItem dot;
    dot.tool = AnnotationTool::Pen;
    dot.size = 0.1;
    dot.points = {{0.2f, 0.5f, 1, 0, 0}};
    QImage image = blank(200, 100);
    {
        QPainter painter(&image);
        painter.translate(200, 0);
        painter.scale(-1, 1); // the viewer's Flip Horizontal transform
        AnnotationRenderer::paintFrame(painter, QRectF(0, 0, 200, 100), frameWith(dot));
    }
    QVERIFY(qAlpha(image.pixel(160, 50)) > 200); // mirrored to 80% across
    QCOMPARE(qAlpha(image.pixel(40, 50)), 0);
}

void TestAnnotationRenderer::singlePointStrokeDrawsADot()
{
    AnnotationItem dot;
    dot.tool = AnnotationTool::Brush;
    dot.size = 0.1;
    dot.points = {{0.5f, 0.5f, 1, 0, 0}};
    QImage image = blank(100, 100);
    AnnotationRenderer::renderOnto(image, frameWith(dot));
    QVERIFY(qAlpha(image.pixel(50, 50)) > 200);
    // Soft brush: the rim is fainter than the core.
    QVERIFY(qAlpha(image.pixel(54, 50)) < qAlpha(image.pixel(50, 50)));
}

namespace {
/// A hollow square tip: easy to recognise and to tell apart from a round brush.
BrushTip squareRingTip(const QString& id, qreal spacing)
{
    BrushTip tip;
    tip.id = id;
    tip.spacing = spacing;
    tip.mask = QImage(32, 32, QImage::Format_Grayscale8);
    tip.mask.fill(0);
    // Written through scanLine: setPixel() on a greyscale image takes a QRgb.
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (x < 4 || x > 27 || y < 4 || y > 27) tip.mask.scanLine(y)[x] = 255;
    return tip;
}

AnnotationItem stampStroke(const QString& tipId, float pressure)
{
    AnnotationItem item;
    item.tool = AnnotationTool::Stamp;
    item.brushId = tipId;
    item.color = QColor(0, 0, 255);
    item.size = 0.2; // 40 px dabs on a 200 px tall image
    item.points = {{0.1f, 0.5f, pressure, 0, 0}, {0.9f, 0.5f, pressure, 0, 0}};
    return item;
}
} // namespace

void TestAnnotationRenderer::stampRepeatsTheTipAtItsSpacing()
{
    BrushTipRegistry::instance().add(squareRingTip(QStringLiteral("ring-wide"), 2.0)); // gaps between dabs
    QImage image = blank(800, 200);
    AnnotationRenderer::renderOnto(image, frameWith(stampStroke(QStringLiteral("ring-wide"), 1.0f)));
    // First dab centred at x = 80 (10%): its ring edge is ink, its centre is not.
    QVERIFY(qAlpha(image.pixel(80 - 19, 100)) > 150);
    QCOMPARE(qAlpha(image.pixel(80, 100)), 0);
    // Spacing 2.0 x 40 px: the next dab is centred 80 px further on, with clear paper between.
    QVERIFY(qAlpha(image.pixel(160 - 19, 100)) > 150);
    QCOMPARE(qAlpha(image.pixel(120, 100)), 0);
    QCOMPARE(qAlpha(image.pixel(120, 30)), 0); // nothing outside the dabs

    // Tight spacing fills the line in.
    BrushTipRegistry::instance().add(squareRingTip(QStringLiteral("ring-tight"), 0.05));
    QImage tight = blank(800, 200);
    AnnotationRenderer::renderOnto(tight, frameWith(stampStroke(QStringLiteral("ring-tight"), 1.0f)));
    for (int x = 120; x < 640; x += 20) QVERIFY(qAlpha(tight.pixel(x, 100 - 19)) > 150);
}

void TestAnnotationRenderer::stampFollowsPressureAndColour()
{
    BrushTipRegistry::instance().add(squareRingTip(QStringLiteral("ring-p"), 2.0));
    QImage heavy = blank(800, 200), light = blank(800, 200);
    AnnotationRenderer::renderOnto(heavy, frameWith(stampStroke(QStringLiteral("ring-p"), 1.0f)));
    AnnotationRenderer::renderOnto(light, frameWith(stampStroke(QStringLiteral("ring-p"), 0.1f)));
    auto inkRows = [](const QImage& img, int x) {
        int rows = 0;
        for (int y = 0; y < img.height(); ++y) rows += qAlpha(img.pixel(x, y)) > 40;
        return rows;
    };
    // A light dab is smaller (its ring is nearer the centre) and fainter.
    QVERIFY(inkRows(heavy, 80 - 18) > inkRows(light, 80 - 18) + 10 || inkRows(light, 80 - 18) == 0);
    int heavyMax = 0, lightMax = 0;
    for (int x = 0; x < 800; ++x) {
        heavyMax = std::max(heavyMax, qAlpha(heavy.pixel(x, 100)));
        lightMax = std::max(lightMax, qAlpha(light.pixel(x, 100)));
    }
    QVERIFY(lightMax < heavyMax);
    // Ink takes the stroke colour.
    const QColor c = QColor::fromRgba(heavy.pixel(80 - 19, 100));
    QVERIFY(c.blue() > 200 && c.red() < 30 && c.green() < 30);
}

void TestAnnotationRenderer::stampWithoutItsTipFallsBackToARoundBrush()
{
    QImage image = blank(800, 200);
    AnnotationRenderer::renderOnto(image, frameWith(stampStroke(QStringLiteral("not-registered"), 1.0f)));
    QVERIFY(qAlpha(image.pixel(400, 100)) > 150); // a solid round stroke along the path
}

void TestAnnotationRenderer::pressureCurveShapesAndFloors()
{
    QCOMPARE(applyPressureCurve(0.0f, PressureCurve::Linear), kMinimumPressure);
    QCOMPARE(applyPressureCurve(-1.0f, PressureCurve::Soft), kMinimumPressure);
    QCOMPARE(applyPressureCurve(std::numeric_limits<float>::quiet_NaN(), PressureCurve::Firm), kMinimumPressure);
    QCOMPARE(applyPressureCurve(1.0f, PressureCurve::Firm), 1.0f);
    QCOMPARE(applyPressureCurve(2.0f, PressureCurve::Soft), 1.0f);
    const float soft = applyPressureCurve(0.5f, PressureCurve::Soft);
    const float linear = applyPressureCurve(0.5f, PressureCurve::Linear);
    const float firm = applyPressureCurve(0.5f, PressureCurve::Firm);
    QVERIFY(soft > linear && linear > firm);
    float previous = 0.0f;
    for (int i = 1; i <= 100; ++i) {
        const float v = applyPressureCurve(i / 100.0f, PressureCurve::Soft);
        QVERIFY(v >= previous);
        previous = v;
    }
}

void TestAnnotationRenderer::builderClampsDecimatesAndFinishes()
{
    StrokeBuilder::Settings settings;
    settings.minimumSpacing = 2.0;
    StrokeBuilder builder(settings, QSizeF(1000, 1000));
    QVERIFY(builder.addSample(QPointF(-0.5, 0.5), 0.5f)); // clamped to x=0
    QCOMPARE(builder.points().constFirst().x, 0.0f);
    QVERIFY(!builder.addSample(QPointF(0.001, 0.5), 0.5f)); // 1 px: dropped
    QVERIFY(builder.addSample(QPointF(0.003, 0.5), 0.5f));  // 3 px: kept
    QVERIFY(!builder.addSample(QPointF(qQNaN(), 0.5), 0.5f));
    builder.addSample(QPointF(0.0035, 0.5), 0.9f); // too close, but it is the true end
    const QVector<StrokePoint> points = builder.finish();
    QCOMPARE(points.constLast().x, 0.0035f);
    QCOMPARE(points.constLast().pressure, 0.9f);
    for (const StrokePoint& p : points) QVERIFY(p.x >= 0.0f && p.x <= 1.0f);
}

void TestAnnotationRenderer::stabilizerSmoothsJitter()
{
    auto jitter = [](qreal stabilizer) {
        StrokeBuilder::Settings settings;
        settings.stabilizer = stabilizer;
        settings.minimumSpacing = 0.0;
        StrokeBuilder builder(settings, QSizeF(1000, 1000));
        for (int i = 0; i < 100; ++i)
            builder.addSample(QPointF(0.1 + i * 0.005, 0.5 + ((i % 2) ? 0.01 : -0.01)), 1.0f);
        qreal total = 0;
        const auto& pts = builder.points();
        for (int i = 1; i < pts.size(); ++i) total += std::abs(pts[i].y - pts[i - 1].y);
        return total;
    };
    QVERIFY(jitter(0.8) < jitter(0.0) * 0.5);
}

void TestAnnotationRenderer::pressureCanBeDisabled()
{
    StrokeBuilder::Settings settings;
    settings.usePressure = false;
    StrokeBuilder builder(settings, QSizeF(100, 100));
    builder.addSample(QPointF(0.5, 0.5), 0.1f);
    QCOMPARE(builder.points().constFirst().pressure, 1.0f);
}

void TestAnnotationRenderer::partialRepaintsMatchAFullRender_data()
{
    QTest::addColumn<int>("tool");
    QTest::addColumn<qreal>("dpr");
    for (qreal dpr : {1.0, 1.5, 2.0}) {
        const QByteArray suffix = QByteArray::number(dpr);
        QTest::newRow("pencil@" + suffix) << int(AnnotationTool::Pencil) << dpr;
        QTest::newRow("brush@" + suffix) << int(AnnotationTool::Brush) << dpr;
        QTest::newRow("pen@" + suffix) << int(AnnotationTool::Pen) << dpr;
        QTest::newRow("marker@" + suffix) << int(AnnotationTool::Marker) << dpr;
        QTest::newRow("stamp@" + suffix) << int(AnnotationTool::Stamp) << dpr;
    }
}

// While drawing, the viewer repaints only small areas around the pen. Each
// area must come out exactly as the same pixels of a full render, or seams
// (grain or edge shifts) would show along the stroke.
void TestAnnotationRenderer::partialRepaintsMatchAFullRender()
{
    QFETCH(int, tool);
    QFETCH(qreal, dpr);
    AnnotationItem item;
    item.id = 1;
    item.tool = AnnotationTool(tool);
    item.color = QColor(20, 40, 200);
    item.size = 0.06;
    item.textureSeed = 7;
    if (item.tool == AnnotationTool::Stamp) {
        BrushTipRegistry::instance().add(squareRingTip(QStringLiteral("ring-partial"), 0.3));
        item.brushId = QStringLiteral("ring-partial");
    }
    // Long enough that most segments lie outside any one tile, so the
    // renderer's skipping of segments that cannot reach the area is covered.
    for (int i = 0; i < 160; ++i)
        item.points.append({float(0.1 + i * 0.005), float(0.5 + 0.25 * std::sin(i / 16.0)),
                            float(0.3 + (i % 7) / 10.0), 0, 0});
    const QSize logical(300, 200);
    const QRectF rect(QPointF(0, 0), QSizeF(logical));
    auto canvas = [&] {
        QImage image((QSizeF(logical) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        return image;
    };
    QImage full = canvas();
    {
        QPainter p(&full);
        AnnotationRenderer::paintItem(p, rect, item);
    }
    QImage tiled = canvas();
    {
        QPainter p(&tiled);
        // Odd-sized, odd-placed tiles, like dirty rects around a moving pen.
        for (int y = 0; y < logical.height(); y += 37)
            for (int x = 0; x < logical.width(); x += 29) {
                p.setClipRect(QRect(x, y, 29, 37));
                AnnotationRenderer::paintItem(p, rect, item);
            }
    }
    int worst = 0;
    for (int y = 0; y < full.height(); ++y)
        for (int x = 0; x < full.width(); ++x) {
            const QRgb a = full.pixel(x, y), b = tiled.pixel(x, y);
            worst = std::max({worst, std::abs(qAlpha(a) - qAlpha(b)), std::abs(qRed(a) - qRed(b)),
                              std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
        }
    // Stamp dabs are resampled tip images; a clipped layer can shift that
    // smooth sampling by a level or two at fractional scale. Invisible, and
    // the same with or without the renderer skipping far-away segments.
    const int tolerance = item.tool == AnnotationTool::Stamp ? 4 : 2;
    QVERIFY2(worst <= tolerance, qPrintable(QStringLiteral("largest channel difference %1").arg(worst)));
}

QTEST_MAIN(TestAnnotationRenderer)
#include "tst_annotationrenderer.moc"
