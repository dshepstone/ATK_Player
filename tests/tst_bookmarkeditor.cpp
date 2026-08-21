#include "timeline/Bookmark.h"
#include "timeline/TimelineModel.h"
#include "playback/PlaybackController.h"
#include "ui/BookmarkPanel.h"
#include "ui/MainWindow.h"
#include "ui/Resources.h"
#include "ui/TimelineWidget.h"
#include "ui/ViewerWidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

using atk::timeline::Bookmark;
using atk::timeline::BookmarkType;
using atk::timeline::TimelineModel;
using atk::ui::BookmarkPanel;
using atk::ui::MainWindow;
using atk::ui::TimelineWidget;
using atk::ui::ViewerWidget;

class TestBookmarkEditor : public QObject {
    Q_OBJECT
private slots:
    void iconResourceLoadsAndWindowUsesIt();
    void pointAndRangeCommandsUseActiveFrames();
    void explicitRangeCreationValidatesAndCopiesReviewRange();
    void bookmarkNavigationClearsStaleScrubPlayhead();
    void rangeActivationUpdatesSingleReviewRangeAndPreservesViewer();
    void rangeGeometryOverlapHitAndBoundarySnap();
    void commandShortcutsRemainConflictFree();
};

void TestBookmarkEditor::iconResourceLoadsAndWindowUsesIt()
{
    atk::ui::ensureResourcesInitialized();
    QIcon icon(QStringLiteral(":/icons/ATK_Player_Icon.png"));
    QVERIFY(!icon.isNull());
    QVERIFY(!icon.pixmap(32, 32).isNull());
    MainWindow window;
    QVERIFY(!window.windowIcon().isNull());
}

void TestBookmarkEditor::pointAndRangeCommandsUseActiveFrames()
{
    MainWindow window;
    auto* timelineWidget = window.findChild<TimelineWidget*>();
    auto* panel = window.findChild<BookmarkPanel*>(QStringLiteral("BookmarkPanel"));
    auto* pointAction = window.findChild<QAction*>(QStringLiteral("playback.addBookmark"));
    auto* rangeAction = window.findChild<QAction*>(QStringLiteral("playback.addRangeBookmark"));
    QVERIFY(timelineWidget && panel && pointAction && rangeAction);
    TimelineModel* model = timelineWidget->model();
    model->setCurrentFrame(42);
    pointAction->trigger();
    QCOMPARE(model->bookmarks().size(), qsizetype(1));
    QVERIFY(!model->bookmarks().front().isRange());
    QCOMPARE(model->bookmarks().front().frame, qint64(42));
    QCOMPARE(model->bookmarks().front().frameLabel(), QStringLiteral("43"));
    const quint64 pointId = model->bookmarks().front().id;
    auto* name = panel->findChild<QLineEdit*>(QStringLiteral("BookmarkName"));
    auto* note = panel->findChild<QPlainTextEdit*>(QStringLiteral("BookmarkNote"));
    auto* color = panel->findChild<QComboBox*>(QStringLiteral("BookmarkColor"));
    auto* editorStart = panel->findChild<QSpinBox*>(QStringLiteral("BookmarkStartFrame"));
    auto* editorEnd = panel->findChild<QSpinBox*>(QStringLiteral("BookmarkEndFrame"));
    QVERIFY(name && note && color && editorStart && editorEnd);
    name->setText(QStringLiteral("Contact Pose"));
    name->editingFinished();
    note->setPlainText(QStringLiteral("Push silhouette\nWatch wrist"));
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(note, &focusOut);
    color->setCurrentIndex(color->findData(4));
    editorStart->setValue(41);
    editorStart->editingFinished();
    QCOMPARE(model->bookmark(pointId)->name, QStringLiteral("Contact Pose"));
    QCOMPARE(model->bookmark(pointId)->note, QStringLiteral("Push silhouette\nWatch wrist"));
    QCOMPARE(model->bookmark(pointId)->colorIndex, 4);
    QCOMPARE(model->bookmark(pointId)->frame, qint64(40));

    model->setViewportRange(20, 30);
    rangeAction->trigger();
    QCOMPARE(model->bookmarks().size(), qsizetype(1));
}

void TestBookmarkEditor::explicitRangeCreationValidatesAndCopiesReviewRange()
{
    MainWindow window;
    auto* timelineWidget = window.findChild<TimelineWidget*>();
    auto* panel = window.findChild<BookmarkPanel*>(QStringLiteral("BookmarkPanel"));
    QVERIFY(timelineWidget && panel);
    TimelineModel* model = timelineWidget->model();
    model->setFrameCount(400);
    auto* createStart = panel->findChild<QSpinBox*>(QStringLiteral("RangeCreationStart"));
    auto* createEnd = panel->findChild<QSpinBox*>(QStringLiteral("RangeCreationEnd"));
    auto* useRange = panel->findChild<QPushButton*>(QStringLiteral("UseCurrentReviewRange"));
    auto* addRange = panel->findChild<QPushButton*>(QStringLiteral("AddRangeBookmark"));
    auto* error = panel->findChild<QLabel*>(QStringLiteral("RangeCreationError"));
    QVERIFY(createStart && createEnd && useRange && addRange && error);

    createStart->setValue(138);
    createEnd->setValue(148);
    QTest::mouseClick(addRange, Qt::LeftButton);
    QCOMPARE(model->bookmarks().size(), qsizetype(1));
    QCOMPARE(model->bookmarks().front().frame, qint64(137));
    QCOMPARE(model->bookmarks().front().endFrame, qint64(147));

    model->setViewportRange(299, 327);
    QTest::mouseClick(useRange, Qt::LeftButton);
    QCOMPARE(createStart->value(), 300);
    QCOMPARE(createEnd->value(), 328);
    createEnd->setValue(330);
    QTest::mouseClick(addRange, Qt::LeftButton);
    QCOMPARE(model->bookmarks().size(), qsizetype(2));
    const Bookmark* adjusted = model->bookmark(panel->selectedBookmarkId());
    QVERIFY(adjusted && adjusted->isRange());
    QCOMPARE(adjusted->frame, qint64(299));
    QCOMPARE(adjusted->endFrame, qint64(329));

    createStart->setValue(200);
    createEnd->setValue(100);
    QTest::mouseClick(addRange, Qt::LeftButton);
    QCOMPARE(model->bookmarks().size(), qsizetype(2));
    QVERIFY(!error->text().isEmpty());
    createStart->setValue(200);
    createEnd->setValue(200);
    QTest::mouseClick(addRange, Qt::LeftButton);
    QCOMPARE(model->bookmarks().size(), qsizetype(2));
}

void TestBookmarkEditor::bookmarkNavigationClearsStaleScrubPlayhead()
{
    MainWindow window;
    auto* widget = window.findChild<TimelineWidget*>();
    auto* panel = window.findChild<BookmarkPanel*>(QStringLiteral("BookmarkPanel"));
    auto* playback = window.playbackController();
    auto* next = window.findChild<QAction*>(QStringLiteral("playback.nextBookmark"));
    auto* previous = window.findChild<QAction*>(QStringLiteral("playback.prevBookmark"));
    QVERIFY(widget && panel && playback && next && previous);
    TimelineModel* model = widget->model();
    model->setFrameCount(260);
    widget->resize(1000, 130);
    for (const auto [start, end] : {std::pair<qint64, qint64>{49, 49}, {99, 119},
                                    {159, 159}, {199, 219}}) {
        Bookmark bookmark;
        bookmark.type = start == end ? BookmarkType::Point : BookmarkType::Range;
        bookmark.frame = start;
        bookmark.endFrame = end;
        model->addBookmark(bookmark);
    }

    // Reproduce the human failure: release a scrub whose synchronous
    // placeholder presentation occurs while m_scrubbing is still true. That
    // leaves the pointer overlay armed until bookmark activation cancels it.
    model->fitViewport();
    QTest::mousePress(widget, Qt::LeftButton, {}, QPoint(widget->positionForFrame(10), 100));
    QTest::mouseRelease(widget, Qt::LeftButton, {}, QPoint(widget->positionForFrame(10), 100));
    QCOMPARE(widget->displayedFrame(), qint64(10));

    for (int run = 0; run < 25; ++run) {
        next->trigger();
        next->trigger();
        next->trigger();
        previous->trigger();
        next->trigger();
        const Bookmark* selected = model->bookmark(panel->selectedBookmarkId());
        QVERIFY(selected);
        QCOMPARE(playback->currentFrame(), selected->frame);
        QCOMPARE(model->currentFrame(), selected->frame);
        QCOMPARE(widget->displayedFrame(), selected->frame);
    }
}

void TestBookmarkEditor::rangeActivationUpdatesSingleReviewRangeAndPreservesViewer()
{
    MainWindow window;
    auto* timelineWidget = window.findChild<TimelineWidget*>();
    auto* panel = window.findChild<BookmarkPanel*>(QStringLiteral("BookmarkPanel"));
    auto* viewer = window.findChild<ViewerWidget*>();
    auto* start = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeStartFrame"));
    auto* end = window.findChild<QSpinBox*>(QStringLiteral("ReviewRangeEndFrame"));
    QVERIFY(timelineWidget && panel && viewer && start && end);
    TimelineModel* model = timelineWidget->model();
    Bookmark range;
    range.type = BookmarkType::Range;
    range.frame = 20;
    range.endFrame = 30;
    const quint64 id = model->addBookmark(range);
    viewer->showActualSize();
    viewer->zoomIn();
    const qreal zoom = viewer->transform().scale();
    const QPointF pan = viewer->transform().pan();
    QSignalSpy activated(panel, &BookmarkPanel::bookmarkActivated);
    panel->selectBookmark(id);
    auto* list = panel->findChild<QListWidget*>(QStringLiteral("BookmarkList"));
    QVERIFY(list && list->currentItem());
    panel->bookmarkActivated(id);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(model->viewport().startFrame(), qint64(20));
    QCOMPARE(model->viewport().endFrame(), qint64(30));
    QCOMPARE(model->currentFrame(), qint64(20));
    QCOMPARE(start->value(), 21);
    QCOMPARE(end->value(), 31);
    QCOMPARE(viewer->transform().scale(), zoom);
    QCOMPARE(viewer->transform().pan(), pan);
}

void TestBookmarkEditor::rangeGeometryOverlapHitAndBoundarySnap()
{
    TimelineModel model;
    model.setFrameCount(300);
    model.setViewportRange(50, 149);
    Bookmark first;
    first.type = BookmarkType::Range;
    first.frame = 100;
    first.endFrame = 120;
    const quint64 firstId = model.addBookmark(first);
    Bookmark second = first;
    second.id = 0;
    second.frame = 110;
    second.endFrame = 130;
    const quint64 secondId = model.addBookmark(second);
    TimelineWidget widget;
    widget.resize(1000, 120);
    widget.setModel(&model);
    const QRect firstRect = widget.rangeBookmarkRect(firstId);
    const QRect secondRect = widget.rangeBookmarkRect(secondId);
    QVERIFY(!firstRect.isEmpty() && !secondRect.isEmpty());
    QVERIFY(firstRect.top() != secondRect.top());
    QCOMPARE(widget.bookmarkAtPosition(firstRect.center()), firstId);
    QVERIFY(firstRect.left() <= widget.positionForFrame(100));
    QVERIFY(firstRect.left() >= widget.positionForFrame(99));
    QVERIFY(firstRect.right() >= widget.positionForFrame(120));
    QCOMPARE(widget.rangeBookmarkStartCapRect(firstId).left(), firstRect.left());
    QCOMPARE(widget.rangeBookmarkEndCapRect(firstId).right(), firstRect.right());
    QVERIFY(widget.rangeBookmarkShowsLabel(firstId));
    widget.setSelectedBookmark(firstId);
    QCOMPARE(widget.selectedBookmarkId(), firstId);

    QSignalSpy preview(&widget, &TimelineWidget::scrubPreviewRequested);
    const int middleX = widget.positionForFrame(110);
    QTest::mouseClick(&widget, Qt::LeftButton, {}, QPoint(middleX, 90));
    QVERIFY(!preview.isEmpty());
    QCOMPARE(preview.last().at(0).toLongLong(), qint64(110));
    preview.clear();
    QTest::mouseClick(&widget, Qt::LeftButton, {}, QPoint(widget.positionForFrame(120) + 3, 90));
    QVERIFY(!preview.isEmpty());
    QCOMPARE(preview.last().at(0).toLongLong(), qint64(120));
}

void TestBookmarkEditor::commandShortcutsRemainConflictFree()
{
    const auto* point = atk::commands::find(atk::commands::CommandId::AddBookmark);
    const auto* range = atk::commands::find(atk::commands::CommandId::AddRangeBookmark);
    const auto* timelineFit = atk::commands::find(atk::commands::CommandId::TimelineZoomFit);
    QVERIFY(point && range && timelineFit);
    QCOMPARE(QString::fromLatin1(point->defaultShortcut), QStringLiteral("B"));
    QVERIFY(range->defaultShortcut == nullptr);
    QCOMPARE(QString::fromLatin1(timelineFit->defaultShortcut), QStringLiteral("F"));
}

QTEST_MAIN(TestBookmarkEditor)
#include "tst_bookmarkeditor.moc"
