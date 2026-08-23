#include "timeline/TimelineModel.h"
#include "timeline/Timecode.h"
#include "ui/TimelineRangeSlider.h"
#include "ui/TimelineWidget.h"
#include "ui/FrameNumberInput.h"
#include "ui/MainWindow.h"
#include "ui/StatusInfoBar.h"
#include "api/ApiServer.h"

#include <QSignalSpy>
#include <QLabel>
#include <QJsonObject>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QSpinBox>
#include <QTest>

using atk::media::FrameRate;
using atk::timeline::Bookmark;
using atk::timeline::TimelineModel;
using atk::ui::TimelineRangeSlider;
using atk::ui::TimelineWidget;
using atk::ui::FrameNumberInput;
using atk::ui::MainWindow;

class TestTimelineWidgets : public QObject {
    Q_OBJECT
private slots:
    void directFrameNumberInput();
    void sliderTracksModelBothWays();
    void sliderHandlesAndBodyEditViewport();
    void frameMappingIsExactAtReviewZoom();
    void bookmarkMarkerAndSnapStayAligned();
    void rationalRatesKeepIntegerFrameDisplay();
    void numericFieldsTrackEveryReviewRangeInput();
    void sliderDoubleClickFitsWithoutMovingPlayhead();
    void stoppedHandleResizeCentresButBodyPanDoesNot();
    void statusUsesOneBasedFramesAndZeroOriginTimecode();
    void releasedScrubFollowsSubsequentAuthoritativeNavigation();
    void activeAndFinalScrubPresentationRemainResponsive();
};

void TestTimelineWidgets::directFrameNumberInput()
{
    FrameNumberInput input;
    auto* field = input.findChild<QSpinBox*>(QStringLiteral("CurrentFrameNumber"));
    auto* editor = field ? field->findChild<QLineEdit*>() : nullptr;
    QVERIFY(field && editor);
    QVERIFY(!field->isEnabled());
    QCOMPARE(field->buttonSymbols(), QAbstractSpinBox::NoButtons);
    QCOMPARE(input.visibleFrame(), 1);

    input.setFrameCount(160);
    QCOMPARE(input.maximumVisibleFrame(), 160);
    QVERIFY(!field->isEnabled());
    input.setMediaAvailable(true);
    QVERIFY(field->isEnabled());

    input.setCurrentFrame(0);
    QCOMPARE(input.visibleFrame(), 1);
    input.setCurrentFrame(1);
    QCOMPARE(input.visibleFrame(), 2);
    input.setCurrentFrame(159);
    QCOMPARE(input.visibleFrame(), 160);

    const int shortRangeWidth = field->width();
    input.setFrameCount(3229);
    QCOMPARE(input.maximumVisibleFrame(), 3229);
    QVERIFY(field->width() > shortRangeWidth);
    input.setCurrentFrame(3228);
    QCOMPARE(input.visibleFrame(), 3229);

    const int mediumRangeWidth = field->width();
    input.setFrameCount(12000);
    QCOMPARE(input.maximumVisibleFrame(), 12000);
    QVERIFY(field->width() > mediumRangeWidth);

    input.setFrameCount(160);
    QCOMPARE(field->width(), shortRangeWidth);
    input.setCurrentFrame(159);

    input.show();
    QCoreApplication::processEvents();
    QVERIFY(input.isVisible());
    input.activateWindow();
    QSignalSpy seeks(&input, &FrameNumberInput::seekFrameRequested);
    editor->setFocus();
    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("121"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(seeks.count(), 1);
    QCOMPARE(seeks.takeFirst().at(0).toLongLong(), qint64(120));
    QTRY_VERIFY(!editor->hasFocus());
    QTest::qWait(1);

    editor->setFocus();
    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("999"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(seeks.count(), 1);
    const auto constrainedFrame = seeks.takeFirst().at(0).toLongLong();
    QVERIFY(constrainedFrame >= 0);
    QVERIFY(constrainedFrame < 160);
    QTRY_VERIFY(!editor->hasFocus());
    QTest::qWait(1);

    input.setFrameCount(36);
    QCOMPARE(input.maximumVisibleFrame(), 36);
    QCOMPARE(input.visibleFrame(), 36);

    editor->setFocus();
    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("12"));
    QTest::keyClick(editor, Qt::Key_Escape);
    QCOMPARE(seeks.count(), 0);
    QCOMPARE(input.visibleFrame(), 36);

    input.setMediaAvailable(false);
    QVERIFY(!field->isEnabled());
    input.setFrameCount(0);
    QCOMPARE(input.maximumVisibleFrame(), 1);

    MainWindow window;
    auto* integratedTimeline = window.findChild<TimelineWidget*>(QStringLiteral("TimelineWidget"));
    auto* integratedField = window.findChild<QSpinBox*>(QStringLiteral("CurrentFrameNumber"));
    QVERIFY(integratedTimeline && integratedField);
    integratedTimeline->model()->setFrameCount(48);
    integratedTimeline->model()->setCurrentFrame(35);
    QCOMPARE(integratedField->maximum(), 48);
    QCOMPARE(integratedField->value(), 36);
    QVERIFY(!integratedField->isEnabled()); // Placeholder/no-media state is never seekable.
}

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
    QCOMPARE(start->buttonSymbols(), QAbstractSpinBox::NoButtons);
    QCOMPARE(end->buttonSymbols(), QAbstractSpinBox::NoButtons);
    QCOMPARE(start->height(), 26);
    QCOMPARE(end->height(), 26);
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

    const int shortRangeWidth = start->width();
    model->setFrameCount(122268);
    QVERIFY(start->width() > shortRangeWidth);
    QCOMPARE(start->width(), end->width());
    QCOMPARE(end->maximum(), 122268);
    model->setViewportRange(110706, 122267);
    QCOMPARE(start->value(), 110707);
    QCOMPARE(end->value(), 122268);

    start->setValue(110708);
    QCOMPARE(model->viewport().startFrame(), qint64(110707));
    QVERIFY(model->viewport().startFrame() < model->viewport().endFrame());
    end->setValue(122267);
    QCOMPARE(model->viewport().endFrame(), qint64(122266));
    QVERIFY(model->viewport().startFrame() < model->viewport().endFrame());

    model->setFrameCount(160);
    QCOMPARE(start->width(), shortRangeWidth);
    QCOMPARE(end->width(), shortRangeWidth);
    QCOMPARE(end->maximum(), 160);
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

void TestTimelineWidgets::releasedScrubFollowsSubsequentAuthoritativeNavigation()
{
    atk::ui::MainWindow window;
    auto* widget = window.findChild<TimelineWidget*>(QStringLiteral("TimelineWidget"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("StatusFrameValue"));
    QVERIFY(widget && status);
    TimelineModel* model = widget->model();
    model->setFrameCount(264);
    model->fitViewport();
    widget->resize(1000, 130);

    const QPoint scrubPoint(widget->positionForFrame(13), widget->height() - 18);
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, scrubPoint);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, scrubPoint);
    QCOMPARE(widget->displayedFrame(), qint64(13));

    const auto response = window.apiServer()->handleRequest(QJsonObject{
        {QStringLiteral("command"), QStringLiteral("seek_frame")},
        {QStringLiteral("params"), QJsonObject{{QStringLiteral("frame"), 50}}}});
    QVERIFY(response.ok);
    QCOMPARE(model->currentFrame(), qint64(50));
    QCOMPARE(widget->displayedFrame(), qint64(50));
    QCOMPARE(status->text(), QStringLiteral("51 / 264"));
    QCOMPARE(widget->positionForFrame(widget->displayedFrame()), widget->positionForFrame(50));

    window.playbackController()->stepForward();
    QCOMPARE(model->currentFrame(), qint64(51));
    QCOMPARE(widget->displayedFrame(), qint64(51));
}

void TestTimelineWidgets::activeAndFinalScrubPresentationRemainResponsive()
{
    TimelineModel model;
    model.setFrameCount(100);
    TimelineWidget widget;
    widget.resize(1000, 130);
    widget.setModel(&model);

    const QPoint target(widget.positionForFrame(40), widget.height() - 18);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier,
                      QPoint(widget.positionForFrame(10), target.y()));
    QTest::mouseMove(&widget, target);
    QCOMPARE(model.currentFrame(), qint64(0));
    QCOMPARE(widget.displayedFrame(), qint64(40));

    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, target);
    model.setCurrentFrame(20);
    QCOMPARE(widget.displayedFrame(), qint64(40));
    model.setCurrentFrame(40);
    QCOMPARE(widget.displayedFrame(), qint64(40));

    model.setCurrentFrame(50);
    QCOMPARE(widget.displayedFrame(), qint64(50));
}

QTEST_MAIN(TestTimelineWidgets)
#include "tst_timelinewidgets.moc"
