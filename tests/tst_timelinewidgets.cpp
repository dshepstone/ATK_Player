#include "timeline/TimelineModel.h"
#include "ui/TimelineRangeSlider.h"
#include "ui/TimelineWidget.h"

#include <QSignalSpy>
#include <QTest>

using atk::media::FrameRate;
using atk::timeline::Bookmark;
using atk::timeline::TimelineModel;
using atk::ui::TimelineRangeSlider;
using atk::ui::TimelineWidget;

class TestTimelineWidgets : public QObject {
    Q_OBJECT
private slots:
    void sliderTracksModelBothWays();
    void sliderHandlesAndBodyEditViewport();
    void frameMappingIsExactAtReviewZoom();
    void bookmarkMarkerAndSnapStayAligned();
    void rationalRatesKeepIntegerFrameDisplay();
};

void TestTimelineWidgets::sliderTracksModelBothWays()
{
    TimelineModel model;
    model.setFrameCount(1000);
    TimelineRangeSlider slider;
    slider.resize(1000, 24);
    slider.setModel(&model);
    QCOMPARE(slider.selectionRect().left(), slider.positionForSourceFrame(0));
    QCOMPARE(slider.selectionRect().right(), slider.positionForSourceFrame(999));
    QCOMPARE(slider.sourceFrameAtPosition(slider.positionForSourceFrame(0)), qint64(0));
    QCOMPARE(slider.sourceFrameAtPosition(slider.positionForSourceFrame(999)), qint64(999));

    model.setViewportRange(200, 299);
    QCOMPARE(slider.selectionRect().left(), slider.positionForSourceFrame(200));
    QCOMPARE(slider.selectionRect().right(), slider.positionForSourceFrame(299));
}

void TestTimelineWidgets::sliderHandlesAndBodyEditViewport()
{
    TimelineModel model;
    model.setFrameCount(1000);
    TimelineRangeSlider slider;
    slider.resize(1000, 24);
    slider.setModel(&model);
    model.setViewportRange(200, 399);

    QPoint left(slider.selectionRect().left(), slider.selectionRect().center().y());
    QTest::mousePress(&slider, Qt::LeftButton, {}, left);
    QTest::mouseMove(&slider, left + QPoint(90, 0));
    QTest::mouseRelease(&slider, Qt::LeftButton, {}, left + QPoint(90, 0));
    QVERIFY(model.viewport().startFrame() > 200);
    QCOMPARE(model.viewport().endFrame(), qint64(399));

    model.setViewportRange(200, 399);
    QPoint right(slider.selectionRect().right(), slider.selectionRect().center().y());
    QTest::mousePress(&slider, Qt::LeftButton, {}, right);
    QTest::mouseMove(&slider, right - QPoint(90, 0));
    QTest::mouseRelease(&slider, Qt::LeftButton, {}, right - QPoint(90, 0));
    QCOMPARE(model.viewport().startFrame(), qint64(200));
    QVERIFY(model.viewport().endFrame() < 399);

    // Handles stop at the ten-frame minimum and never cross.
    model.setViewportRange(200, 399);
    left = QPoint(slider.selectionRect().left(), slider.selectionRect().center().y());
    QTest::mousePress(&slider, Qt::LeftButton, {}, left);
    QTest::mouseMove(&slider, QPoint(slider.selectionRect().right() + 200, left.y()));
    QTest::mouseRelease(&slider, Qt::LeftButton, {}, QPoint(slider.selectionRect().right() + 200, left.y()));
    QCOMPARE(model.viewport().visibleFrameCount(), qint64(10));
    QVERIFY(model.viewport().startFrame() < model.viewport().endFrame());

    model.setViewportRange(200, 399);
    const qint64 span = model.viewport().visibleFrameCount();
    QPoint body = slider.selectionRect().center();
    QTest::mousePress(&slider, Qt::LeftButton, {}, body);
    QTest::mouseMove(&slider, body + QPoint(90, 0));
    QTest::mouseRelease(&slider, Qt::LeftButton, {}, body + QPoint(90, 0));
    QVERIFY(model.viewport().startFrame() > 200);
    QCOMPARE(model.viewport().visibleFrameCount(), span);
}

void TestTimelineWidgets::frameMappingIsExactAtReviewZoom()
{
    TimelineModel model;
    model.setFrameCount(1000);
    model.setViewportRange(100, 119);
    TimelineWidget widget;
    widget.resize(1000, 110);
    widget.setModel(&model);
    for (qint64 frame = 100; frame <= 119; ++frame)
        QCOMPARE(widget.frameAtPosition(widget.positionForFrame(frame)), frame);
    QCOMPARE(widget.positionForFrame(100), 52);
    QCOMPARE(widget.positionForFrame(119), widget.width() - 53);
}

void TestTimelineWidgets::bookmarkMarkerAndSnapStayAligned()
{
    TimelineModel model;
    model.setFrameCount(101);
    Bookmark bookmark;
    bookmark.frame = 50;
    model.addBookmark(bookmark);
    TimelineWidget widget;
    widget.resize(1000, 110);
    widget.setModel(&model);
    const int markerX = widget.positionForFrame(50);
    QCOMPARE(widget.frameAtPosition(markerX), qint64(50));

    QSignalSpy preview(&widget, &TimelineWidget::scrubPreviewRequested);
    QTest::mouseClick(&widget, Qt::LeftButton, {}, QPoint(markerX + 7, 80));
    QVERIFY(!preview.isEmpty());
    QCOMPARE(preview.last().at(0).toLongLong(), qint64(50));

    widget.setBookmarkSnapEnabled(false);
    preview.clear();
    QTest::mouseClick(&widget, Qt::LeftButton, {}, QPoint(markerX + 7, 80));
    QVERIFY(!preview.isEmpty());
    QVERIFY(preview.last().at(0).toLongLong() != qint64(50));
}

void TestTimelineWidgets::rationalRatesKeepIntegerFrameDisplay()
{
    for (const FrameRate rate : {FrameRate{24000, 1001}, FrameRate{30000, 1001}, FrameRate{60000, 1001}}) {
        TimelineModel model;
        model.setFrameRate(rate);
        model.setFrameCount(200);
        model.setViewportRange(100, 119);
        TimelineWidget widget;
        widget.resize(1000, 110);
        widget.setModel(&model);
        QCOMPARE(widget.frameAtPosition(widget.positionForFrame(107)), qint64(107));
        QCOMPARE(model.mediaTimeForFrame(107), static_cast<qint64>(
            static_cast<long double>(107) * 1'000'000.0L * rate.denominator / rate.numerator));
    }
}

QTEST_MAIN(TestTimelineWidgets)
#include "tst_timelinewidgets.moc"
