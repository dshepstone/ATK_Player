#include "timeline/TimelineModel.h"
#include "timeline/Timecode.h"
#include "ui/TimelineRangeSlider.h"
#include "ui/TimelineWidget.h"
#include "ui/MainWindow.h"
#include "ui/StatusInfoBar.h"

#include <QSignalSpy>
#include <QLabel>
#include <QSpinBox>
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
    void numericFieldsTrackEveryReviewRangeInput();
    void sliderDoubleClickFitsWithoutMovingPlayhead();
    void stoppedHandleResizeCentresButBodyPanDoesNot();
    void statusUsesOneBasedFramesAndZeroOriginTimecode();
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

void TestTimelineWidgets::numericFieldsTrackEveryReviewRangeInput()
{
    atk::ui::MainWindow window;
    auto* slider = window.findChild<TimelineRangeSlider*>(QStringLiteral("TimelineReviewRangeSlider"));
    auto* start = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeStartFrame"));
    auto* end = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeEndFrame"));
    auto* statusFrame = window.findChild<QLabel*>(QStringLiteral("StatusFrameValue"));
    QVERIFY(slider && start && end && statusFrame);
    TimelineModel* model = slider->model();
    QCOMPARE(start->value(), 1);
    QCOMPARE(end->value(), 100);

    model->setViewportRange(19, 29);
    QCOMPARE(start->value(), 20);
    QCOMPARE(end->value(), 30);
    start->setValue(15);
    QCOMPARE(model->viewport().startFrame(), qint64(14));
    end->setValue(40);
    QCOMPARE(model->viewport().endFrame(), qint64(39));

    model->zoomViewport(2.0, 25);
    QCOMPARE(start->value(), int(model->viewport().startFrame() + 1));
    QCOMPARE(end->value(), int(model->viewport().endFrame() + 1));
    model->panViewport(5);
    QCOMPARE(start->value(), int(model->viewport().startFrame() + 1));
    QCOMPARE(end->value(), int(model->viewport().endFrame() + 1));
    QVERIFY(end->value() - start->value() + 1 >= 10);

    model->setViewportRange(44, 74);
    model->setCurrentFrame(44);
    QCOMPARE(start->value(), 45);
    QCOMPARE(end->value(), 75);
    QCOMPARE(statusFrame->text(), QStringLiteral("45 / 100"));
}

void TestTimelineWidgets::sliderDoubleClickFitsWithoutMovingPlayhead()
{
    atk::ui::MainWindow window;
    auto* slider = window.findChild<TimelineRangeSlider*>(QStringLiteral("TimelineReviewRangeSlider"));
    auto* start = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeStartFrame"));
    auto* end = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeEndFrame"));
    QVERIFY(slider && start && end);
    slider->resize(900, 24);
    TimelineModel* model = slider->model();
    model->setCurrentFrame(42);
    model->setViewportRange(20, 40);
    const qsizetype bookmarkCount = model->bookmarks().size();
    QSignalSpy playhead(model, &TimelineModel::currentFrameChanged);
    QTest::mouseDClick(slider, Qt::LeftButton, {}, slider->selectionRect().center());
    QCOMPARE(model->viewport().startFrame(), qint64(0));
    QCOMPARE(model->viewport().endFrame(), qint64(99));
    QCOMPARE(start->value(), 1);
    QCOMPARE(end->value(), 100);
    QCOMPARE(model->currentFrame(), qint64(42));
    QCOMPARE(playhead.count(), 0);
    QCOMPARE(model->bookmarks().size(), bookmarkCount);

    model->setViewportRange(20, 40);
    QTest::mouseDClick(slider, Qt::LeftButton, {},
                      QPoint(slider->positionForSourceFrame(80), slider->selectionRect().center().y()));
    QCOMPARE(model->viewport().startFrame(), qint64(0));
    QCOMPARE(model->viewport().endFrame(), qint64(99));

    model->setViewportRange(20, 40);
    QTest::mouseDClick(start, Qt::LeftButton, {}, start->rect().center());
    QCOMPARE(model->viewport().startFrame(), qint64(20));
    QCOMPARE(model->viewport().endFrame(), qint64(40));
}

void TestTimelineWidgets::stoppedHandleResizeCentresButBodyPanDoesNot()
{
    atk::ui::MainWindow window;
    auto* slider = window.findChild<TimelineRangeSlider*>(QStringLiteral("TimelineReviewRangeSlider"));
    auto* startField = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeStartFrame"));
    auto* endField = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeEndFrame"));
    QVERIFY(slider && startField && endField);
    slider->resize(1000, 24);
    TimelineModel* model = slider->model();
    model->setFrameCount(400);

    model->setViewportRange(100, 148);
    model->setCurrentFrame(300);
    QPoint left(slider->selectionRect().left(), slider->selectionRect().center().y());
    const QPoint newLeft(slider->positionForSourceFrame(138), left.y());
    QTest::mousePress(slider, Qt::LeftButton, {}, left);
    QTest::mouseMove(slider, newLeft);
    QTest::mouseRelease(slider, Qt::LeftButton, {}, newLeft);
    QCOMPARE(model->viewport().startFrame(), qint64(138));
    QCOMPARE(model->viewport().endFrame(), qint64(148));
    QCOMPARE(model->currentFrame(), qint64(143));

    model->setViewportRange(138, 200);
    model->setCurrentFrame(300);
    QPoint right(slider->selectionRect().right(), slider->selectionRect().center().y());
    const QPoint newRight(slider->positionForSourceFrame(148), right.y());
    QTest::mousePress(slider, Qt::LeftButton, {}, right);
    QTest::mouseMove(slider, newRight);
    QTest::mouseRelease(slider, Qt::LeftButton, {}, newRight);
    QCOMPARE(model->viewport().startFrame(), qint64(138));
    QCOMPARE(model->viewport().endFrame(), qint64(148));
    QCOMPARE(model->currentFrame(), qint64(143));
    QCOMPARE(slider->positionForSourceFrame(model->currentFrame()),
             slider->selectionRect().center().x());

    startField->setValue(100);
    endField->setValue(120);
    QCOMPARE(model->currentFrame(), qint64(109)); // visible frame 110

    model->setViewportRange(99, 119);
    model->setCurrentFrame(109);
    const qint64 oldFrame = model->currentFrame();
    const QPoint body = slider->selectionRect().center();
    const int delta = slider->positionForSourceFrame(300)
                    - slider->positionForSourceFrame(99);
    QTest::mousePress(slider, Qt::LeftButton, {}, body);
    QTest::mouseMove(slider, body + QPoint(delta, 0));
    QTest::mouseRelease(slider, Qt::LeftButton, {}, body + QPoint(delta, 0));
    QCOMPARE(model->viewport().startFrame(), qint64(300));
    QCOMPARE(model->viewport().endFrame(), qint64(320));
    QCOMPARE(model->currentFrame(), oldFrame);
}

void TestTimelineWidgets::statusUsesOneBasedFramesAndZeroOriginTimecode()
{
    TimelineModel model;
    model.setFrameRate(FrameRate{24, 1});
    atk::ui::StatusInfoBar status;
    status.setModel(&model);

    auto* frame = status.findChild<QLabel*>(QStringLiteral("StatusFrameValue"));
    auto* timecode = status.findChild<QLabel*>(QStringLiteral("StatusTimecodeValue"));
    QVERIFY(frame && timecode);

    QCOMPARE(frame->text(), QStringLiteral("0 / 0"));
    model.setFrameCount(305);
    QCOMPARE(frame->text(), QStringLiteral("1 / 305"));
    QCOMPARE(timecode->text(), QStringLiteral("00:00:00:00"));

    model.setCurrentFrame(44);
    QCOMPARE(model.currentFrame(), qint64(44));
    QCOMPARE(frame->text(), QStringLiteral("45 / 305"));
    QCOMPARE(timecode->text(), QStringLiteral("00:00:01:20"));

    model.setCurrentFrame(304);
    QCOMPARE(frame->text(), QStringLiteral("305 / 305"));
    QCOMPARE(timecode->text(), QStringLiteral("00:00:12:16"));

    model.reset();
    QCOMPARE(frame->text(), QStringLiteral("0 / 0"));
    QCOMPARE(timecode->text(), atk::timeline::timecode::placeholder());
}

QTEST_MAIN(TestTimelineWidgets)
#include "tst_timelinewidgets.moc"
