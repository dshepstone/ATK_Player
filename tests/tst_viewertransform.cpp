#include "core/commands/CommandDefinitions.h"
#include "media/VideoFrame.h"
#include "timeline/TimelineModel.h"
#include "ui/ComparisonCompositeWidget.h"
#include "ui/ViewerTransform.h"
#include "ui/ViewerWidget.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>

using atk::commands::CommandId;
using atk::media::VideoFrame;
using atk::timeline::TimelineModel;
using atk::ui::ViewerTransform;
using atk::ui::ViewerWidget;

namespace {

VideoFrame frame(int width, int height, int64_t index = 0)
{
    VideoFrame result;
    result.frameIndex = index;
    result.width = width;
    result.height = height;
    result.image = QImage(width, height, QImage::Format_RGB32);
    result.image.fill(Qt::black);
    return result;
}

/// Left half red, right half blue, so a mirror is unambiguous.
VideoFrame halves(int width, int height, QColor left = Qt::red, QColor right = Qt::blue)
{
    VideoFrame result = frame(width, height);
    result.image.fill(right);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width / 2; ++x) result.image.setPixelColor(x, y, left);
    return result;
}

/// Colour of the widget's rendered output at logical point (x, y).
QColor renderedAt(QWidget& widget, int x, int y)
{
    const QImage image = widget.grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    return image.pixelColor(qRound(x * ratio), qRound(y * ratio));
}

void closeTo(qreal actual, qreal expected, qreal tolerance = 0.001)
{
    QVERIFY2(std::abs(actual - expected) <= tolerance,
             qPrintable(QStringLiteral("%1 is not within %2 of %3")
                            .arg(actual).arg(tolerance).arg(expected)));
}

} // namespace

class TestViewerTransform : public QObject {
    Q_OBJECT
private slots:
    void fitScaleAndCentre();
    void actualSizeHonoursDevicePixels();
    void cursorAnchoredZoom();
    void panAndClamp();
    void smallImageAxesStayCentred();
    void fitReset();
    void viewerDoubleClickFits();
    void sourceReplacementResetsToFit();
    void resizeRules();
    void transformPersistsThroughFrameChange();
    void viewerNavigationDoesNotTouchTimeline();
    void timelineFRemainsIndependent();
    void flipMirrorsPictureWithoutTouchingNavigation();
    void flipMirrorsCompositeAndWipeDrag();
    void flipCommandIsCheckableWithHShortcut();
};

void TestViewerTransform::fitScaleAndCentre()
{
    ViewerTransform transform;
    transform.setViewportSize({1280, 720});
    transform.setSourceSize({1920, 1080}, true);
    closeTo(transform.scale(), 2.0 / 3.0);
    QCOMPARE(transform.imageRect(), QRectF(0, 0, 1280, 720));

    transform.setViewportSize({1000, 1000});
    closeTo(transform.scale(), 1000.0 / 1920.0);
    closeTo(transform.imageRect().top(), 218.75);
    closeTo(transform.imageRect().center().x(), 500.0);
    closeTo(transform.imageRect().center().y(), 500.0);
}

void TestViewerTransform::actualSizeHonoursDevicePixels()
{
    ViewerTransform transform;
    transform.setDevicePixelRatio(2.0);
    transform.setViewportSize({1000, 700});
    transform.setSourceSize({1920, 1080}, true);
    transform.setActualSize();
    closeTo(transform.scale(), 0.5);
    closeTo(transform.zoomRatio(), 1.0);
    closeTo(transform.imageRect().width(), 960.0);
}

void TestViewerTransform::cursorAnchoredZoom()
{
    ViewerTransform transform;
    transform.setViewportSize({800, 600});
    transform.setSourceSize({800, 600}, true);
    const QPointF cursor(250, 180);
    const QPointF before = transform.viewerToImage(cursor);
    transform.zoomAt(2.0, cursor);
    const QPointF after = transform.viewerToImage(cursor);
    closeTo(after.x(), before.x());
    closeTo(after.y(), before.y());
}

void TestViewerTransform::panAndClamp()
{
    ViewerTransform transform;
    transform.setViewportSize({800, 600});
    transform.setSourceSize({800, 600}, true);
    transform.zoomAt(2.0, {400, 300});
    transform.panBy({100, -80});
    QCOMPARE(transform.pan(), QPointF(100, -80));
    transform.panBy({10000, -10000});
    QCOMPARE(transform.imageRect().left(), 0.0);
    QCOMPARE(transform.imageRect().bottom(), 600.0);
}

void TestViewerTransform::smallImageAxesStayCentred()
{
    ViewerTransform transform;
    transform.setViewportSize({1000, 600});
    transform.setSourceSize({400, 800}, true);
    transform.setActualSize();
    transform.panBy({200, 200});
    QCOMPARE(transform.imageRect().center().x(), 500.0);
    QVERIFY(transform.imageRect().center().y() > 300.0);
}

void TestViewerTransform::fitReset()
{
    ViewerTransform transform;
    transform.setViewportSize({1000, 600});
    transform.setSourceSize({1920, 1080}, true);
    transform.zoomAt(3.0, {300, 200});
    transform.panBy({50, 20});
    transform.fit();
    QVERIFY(transform.isFit());
    QCOMPARE(transform.pan(), QPointF());
    closeTo(transform.imageRect().center().x(), 500.0);
    closeTo(transform.imageRect().center().y(), 300.0);
}

void TestViewerTransform::viewerDoubleClickFits()
{
    ViewerWidget viewer;
    viewer.resize(800, 600);
    viewer.setFrame(frame(800, 600));
    viewer.showActualSize();
    viewer.zoomIn();
    QVERIFY(!viewer.transform().isFit());
    QTest::mouseDClick(&viewer, Qt::LeftButton, {}, viewer.rect().center());
    QVERIFY(viewer.transform().isFit());
}

void TestViewerTransform::sourceReplacementResetsToFit()
{
    ViewerWidget viewer;
    viewer.resize(800, 600);
    viewer.setFrame(frame(800, 600));
    viewer.showActualSize();
    viewer.zoomIn();
    viewer.resetNavigationToFit();
    viewer.setFrame(frame(1920, 1080));
    QVERIFY(viewer.transform().isFit());
    closeTo(viewer.transform().imageRect().center().x(), 400.0);
    closeTo(viewer.transform().imageRect().center().y(), 300.0);
}

void TestViewerTransform::resizeRules()
{
    ViewerTransform transform;
    transform.setViewportSize({800, 600});
    transform.setSourceSize({1920, 1080}, true);
    const qreal fitBefore = transform.scale();
    transform.setViewportSize({1000, 700});
    QVERIFY(transform.scale() != fitBefore);

    transform.setActualSize();
    transform.zoomAt(2.0, {500, 350});
    const qreal customBefore = transform.scale();
    const QPointF focusBefore = transform.viewerToImage({500, 350});
    transform.setViewportSize({1200, 800});
    QCOMPARE(transform.scale(), customBefore);
    const QPointF focusAfter = transform.viewerToImage({600, 400});
    closeTo(focusAfter.x(), focusBefore.x());
    closeTo(focusAfter.y(), focusBefore.y());
}

void TestViewerTransform::transformPersistsThroughFrameChange()
{
    ViewerWidget viewer;
    viewer.resize(800, 600);
    viewer.setFrame(frame(800, 600, 0));
    viewer.showActualSize();
    viewer.zoomIn();
    const qreal scale = viewer.transform().scale();
    const QPointF pan = viewer.transform().pan();
    viewer.setFrame(frame(800, 600, 1));
    QCOMPARE(viewer.transform().scale(), scale);
    QCOMPARE(viewer.transform().pan(), pan);
}

void TestViewerTransform::viewerNavigationDoesNotTouchTimeline()
{
    TimelineModel timeline;
    timeline.setFrameCount(500);
    timeline.setViewportRange(100, 199);
    QSignalSpy viewportChanged(&timeline, &TimelineModel::viewportChanged);
    QSignalSpy frameChanged(&timeline, &TimelineModel::currentFrameChanged);
    ViewerWidget viewer;
    viewer.resize(800, 600);
    viewer.setFrame(frame(1920, 1080));
    viewer.zoomIn();
    viewer.transform();
    viewer.fitImage();
    QCOMPARE(timeline.viewport().startFrame(), qint64(100));
    QCOMPARE(timeline.viewport().endFrame(), qint64(199));
    QCOMPARE(viewportChanged.count(), 0);
    QCOMPARE(frameChanged.count(), 0);
}

void TestViewerTransform::timelineFRemainsIndependent()
{
    const auto* timelineFit = atk::commands::find(CommandId::TimelineZoomFit);
    const auto* viewerFit = atk::commands::find(CommandId::ZoomFit);
    QVERIFY(timelineFit && viewerFit);
    QCOMPARE(QString::fromLatin1(timelineFit->defaultShortcut), QStringLiteral("F"));
    QCOMPARE(QString::fromLatin1(viewerFit->defaultShortcut), QStringLiteral("Ctrl+0"));
}

void TestViewerTransform::flipMirrorsPictureWithoutTouchingNavigation()
{
    ViewerWidget viewer;
    viewer.resize(400, 200);
    viewer.setFrame(halves(400, 200));
    const QRectF before = viewer.transform().imageRect();
    QCOMPARE(renderedAt(viewer, 50, 150), QColor(Qt::red));
    QCOMPARE(renderedAt(viewer, 350, 150), QColor(Qt::blue));

    viewer.setFlipHorizontal(true);
    QVERIFY(viewer.isFlippedHorizontally());
    QCOMPARE(renderedAt(viewer, 50, 150), QColor(Qt::blue));
    QCOMPARE(renderedAt(viewer, 350, 150), QColor(Qt::red));
    // Presentation only: zoom/pan geometry is identical.
    QCOMPARE(viewer.transform().imageRect(), before);

    // Navigation snapshot/restore (Video Full Screen) does not carry or clear it.
    viewer.zoomIn();
    const ViewerTransform zoomed = viewer.transform();
    viewer.fitImage();
    viewer.restoreTransform(zoomed);
    QVERIFY(viewer.isFlippedHorizontally());

    // A flipped, zoomed picture still mirrors about its own centre.
    viewer.fitImage();
    viewer.setFlipHorizontal(false);
    QCOMPARE(renderedAt(viewer, 50, 150), QColor(Qt::red));
}

void TestViewerTransform::flipMirrorsCompositeAndWipeDrag()
{
    atk::ui::ComparisonCompositeWidget composite;
    composite.resize(400, 200);
    composite.setMode(atk::playback::CompareLayout::Wipe);
    composite.setWipePosition(25);
    VideoFrame a = frame(400, 200); a.image.fill(Qt::red);
    VideoFrame b = frame(400, 200); b.image.fill(Qt::blue);
    composite.setFrameA(a);
    composite.setFrameB(b);
    // Unflipped: the left quarter is A.
    QCOMPARE(renderedAt(composite, 40, 150), QColor(Qt::red));
    QCOMPARE(renderedAt(composite, 360, 150), QColor(Qt::blue));

    composite.setFlipHorizontal(true);
    QCOMPARE(renderedAt(composite, 40, 150), QColor(Qt::blue));
    QCOMPARE(renderedAt(composite, 360, 150), QColor(Qt::red));

    // Dragging maps through the mirror: 100 px from the left edge on screen is
    // 75% across the composite.
    QSignalSpy wipe(&composite, &atk::ui::ComparisonCompositeWidget::wipePositionChanged);
    QTest::mousePress(&composite, Qt::LeftButton, {}, QPoint(100, 100));
    QTest::mouseRelease(&composite, Qt::LeftButton, {}, QPoint(100, 100));
    QCOMPARE(composite.wipePosition(), 75);
    QCOMPARE(wipe.size(), 1);

    // The composite shows the same FLIPPED H indicator as the dual viewers.
    // A uniform picture makes the badge the only difference flipping makes.
    VideoFrame grey = frame(400, 200); grey.image.fill(QColor(90, 90, 90));
    composite.setFrameA(grey);
    composite.setFrameB(grey);
    composite.setFlipHorizontal(false);
    const QImage plain = composite.grab().toImage();
    composite.setFlipHorizontal(true);
    const QImage flipped = composite.grab().toImage();
    const qreal ratio = plain.devicePixelRatio();
    const QRect badge(qRound(250 * ratio), 0, qRound(150 * ratio), qRound(30 * ratio));
    QVERIFY(plain.copy(badge) != flipped.copy(badge));
}

void TestViewerTransform::flipCommandIsCheckableWithHShortcut()
{
    const auto* flip = atk::commands::find(CommandId::FlipHorizontal);
    QVERIFY(flip);
    QCOMPARE(QString::fromLatin1(flip->key), QStringLiteral("view.flipHorizontal"));
    QVERIFY(flip->checkable);
    QCOMPARE(QKeySequence(QString::fromLatin1(flip->defaultShortcut)), QKeySequence(QStringLiteral("H")));
}

QTEST_MAIN(TestViewerTransform)
#include "tst_viewertransform.moc"
