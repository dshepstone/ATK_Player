#include "core/commands/CommandDefinitions.h"
#include "media/VideoFrame.h"
#include "timeline/TimelineModel.h"
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

QTEST_MAIN(TestViewerTransform)
#include "tst_viewertransform.moc"
