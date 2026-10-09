#include "annotation/AnnotationStore.h"
#include "annotation/AnnotationTypes.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

using namespace atk::annotation;

namespace {
AnnotationItem stroke(float x = 0.5f, float y = 0.5f)
{
    AnnotationItem item;
    item.tool = AnnotationTool::Pen;
    item.points = {{x, y, 0.8f, 0.0f, 0.0f}, {x + 0.1f, y + 0.1f, 0.9f, 0.0f, 0.0f}};
    return item;
}
} // namespace

class TestAnnotationStore : public QObject {
    Q_OBJECT
private slots:
    void toolNamesRoundTripAndRejectUnknown();
    void validityRulesPerKind();
    void addAssignsIncreasingIdsAndRejectsInvalid();
    void framesAreIsolatedAndEmptyFramesAreNotStored();
    void removeItemAndEraserHelper();
    void clearFrameAndClearAll();
    void undoRedoRestoresExactState();
    void newEditClearsRedo();
    void undoHistoryIsBounded();
    void loadingClearsHistoryAndSeedsIds();
    void limitsAreEnforced();
    void trimDropsFramesBeyondEnd();
    void signalsFireOncePerEdit();
};

void TestAnnotationStore::toolNamesRoundTripAndRejectUnknown()
{
    for (AnnotationTool tool : {AnnotationTool::Pencil, AnnotationTool::Pen, AnnotationTool::Brush,
                                AnnotationTool::Marker, AnnotationTool::Eraser, AnnotationTool::Line,
                                AnnotationTool::Arrow, AnnotationTool::Rectangle,
                                AnnotationTool::Ellipse, AnnotationTool::Text}) {
        AnnotationTool parsed = AnnotationTool::Text;
        QVERIFY2(toolFromName(toolName(tool), &parsed), qPrintable(toolName(tool)));
        QCOMPARE(parsed, tool);
    }
    AnnotationTool untouched = AnnotationTool::Pen;
    QVERIFY(!toolFromName(QStringLiteral("laser"), &untouched));
    QCOMPARE(untouched, AnnotationTool::Pen);
    QVERIFY(!isStoredTool(AnnotationTool::Eraser));
}

void TestAnnotationStore::validityRulesPerKind()
{
    QVERIFY(isValidItem(stroke()));

    AnnotationItem eraser = stroke();
    eraser.tool = AnnotationTool::Eraser;
    QVERIFY(!isValidItem(eraser));

    AnnotationItem noPoints = stroke();
    noPoints.points.clear();
    QVERIFY(!isValidItem(noPoints));

    AnnotationItem shape = stroke();
    shape.tool = AnnotationTool::Rectangle;
    QVERIFY(isValidItem(shape));
    shape.points.append({0.2f, 0.2f});
    QVERIFY(!isValidItem(shape)); // shapes are exactly two points

    AnnotationItem text;
    text.tool = AnnotationTool::Text;
    text.points = {{0.3f, 0.3f, 1.0f, 0.0f, 0.0f}};
    QVERIFY(!isValidItem(text)); // text needs text
    text.text = QStringLiteral("fix the nose");
    QVERIFY(isValidItem(text));
    AnnotationItem strokeWithText = stroke();
    strokeWithText.text = QStringLiteral("nope");
    QVERIFY(!isValidItem(strokeWithText));

    AnnotationItem offImage = stroke(1.5f, 0.5f);
    QVERIFY(!isValidItem(offImage));
    AnnotationItem nan = stroke();
    nan.points[0].x = std::numeric_limits<float>::quiet_NaN();
    QVERIFY(!isValidItem(nan));
    AnnotationItem badPressure = stroke();
    badPressure.points[0].pressure = 1.2f;
    QVERIFY(!isValidItem(badPressure));

    AnnotationItem tiny = stroke();
    tiny.size = 0.0;
    QVERIFY(!isValidItem(tiny));
    AnnotationItem huge = stroke();
    huge.size = 2.0;
    QVERIFY(!isValidItem(huge));
    AnnotationItem clear = stroke();
    clear.opacity = 1.5;
    QVERIFY(!isValidItem(clear));
    AnnotationItem noColour = stroke();
    noColour.color = QColor();
    QVERIFY(!isValidItem(noColour));
}

void TestAnnotationStore::addAssignsIncreasingIdsAndRejectsInvalid()
{
    AnnotationStore store;
    const quint64 first = store.addItem(10, 416666, stroke());
    const quint64 second = store.addItem(10, 416666, stroke(0.2f, 0.2f));
    QVERIFY(first != 0);
    QCOMPARE(second, first + 1);
    QCOMPARE(store.frame(10).items.size(), 2);
    QCOMPARE(store.frame(10).mediaTimeUs, qint64(416666));

    AnnotationItem invalid = stroke();
    invalid.points.clear();
    QCOMPARE(store.addItem(10, 0, invalid), quint64(0));
    QCOMPARE(store.addItem(-1, 0, stroke()), quint64(0));
    QCOMPARE(store.frame(10).items.size(), 2);
    QVERIFY(!store.canUndo() || store.annotations().frameCount() == 1);
}

void TestAnnotationStore::framesAreIsolatedAndEmptyFramesAreNotStored()
{
    AnnotationStore store;
    store.addItem(3, 0, stroke());
    store.addItem(7, 0, stroke());
    QCOMPARE(store.annotatedFrames(), QVector<int64_t>({3, 7}));
    QVERIFY(!store.hasAnnotations(5));
    QVERIFY(store.frame(5).isEmpty());
    QCOMPARE(store.frame(5).frame, qint64(5));
    QVERIFY(store.clearFrame(3));
    QCOMPARE(store.annotatedFrames(), QVector<int64_t>({7}));
}

void TestAnnotationStore::removeItemAndEraserHelper()
{
    AnnotationStore store;
    const quint64 a = store.addItem(1, 0, stroke());
    const quint64 b = store.addItem(1, 0, stroke(0.2f, 0.2f));
    const quint64 c = store.addItem(1, 0, stroke(0.3f, 0.3f));

    QVERIFY(!store.removeItem(1, 9999));
    QVERIFY(!store.removeItem(2, a));
    // Eraser: of the hit candidates {a, b}, the later-drawn one (b) goes first.
    QCOMPARE(store.removeLastItemAmong(1, {a, b}), b);
    QCOMPARE(store.frame(1).items.size(), 2);
    QCOMPARE(store.removeLastItemAmong(1, {9999}), quint64(0));
    QVERIFY(store.removeItem(1, a));
    QVERIFY(store.removeItem(1, c));
    QVERIFY(!store.hasAnnotations(1)); // last item gone => frame gone
}

void TestAnnotationStore::clearFrameAndClearAll()
{
    AnnotationStore store;
    QVERIFY(!store.clearFrame(0));
    QVERIFY(!store.clearAll());
    store.addItem(0, 0, stroke());
    store.addItem(5, 0, stroke());
    QVERIFY(store.clearAll());
    QVERIFY(store.annotations().isEmpty());
    QVERIFY(store.undo());
    QCOMPARE(store.annotatedFrames(), QVector<int64_t>({0, 5}));
}

void TestAnnotationStore::undoRedoRestoresExactState()
{
    AnnotationStore store;
    QVERIFY(!store.undo());
    QVERIFY(!store.redo());

    store.addItem(4, 100, stroke());
    const SourceAnnotations afterFirst = store.annotations();
    store.addItem(4, 100, stroke(0.2f, 0.2f));
    const SourceAnnotations afterSecond = store.annotations();
    store.clearFrame(4);
    QVERIFY(store.annotations().isEmpty());

    QVERIFY(store.undo());
    QCOMPARE(store.annotations(), afterSecond);
    QVERIFY(store.undo());
    QCOMPARE(store.annotations(), afterFirst);
    QVERIFY(store.undo());
    QVERIFY(store.annotations().isEmpty());
    QVERIFY(!store.canUndo());

    QVERIFY(store.redo());
    QCOMPARE(store.annotations(), afterFirst);
    QVERIFY(store.redo());
    QCOMPARE(store.annotations(), afterSecond);
    QVERIFY(store.redo());
    QVERIFY(store.annotations().isEmpty());
    QVERIFY(!store.canRedo());
}

void TestAnnotationStore::newEditClearsRedo()
{
    AnnotationStore store;
    store.addItem(1, 0, stroke());
    QVERIFY(store.undo());
    QVERIFY(store.canRedo());
    store.addItem(2, 0, stroke());
    QVERIFY(!store.canRedo());
}

void TestAnnotationStore::undoHistoryIsBounded()
{
    AnnotationStore store;
    for (int i = 0; i < AnnotationStore::kUndoLimit + 25; ++i)
        QVERIFY(store.addItem(i, 0, stroke()) != 0);
    int undone = 0;
    while (store.undo()) ++undone;
    QCOMPARE(undone, AnnotationStore::kUndoLimit);
    QCOMPARE(store.annotations().frameCount(), 25); // oldest 25 edits are permanent
}

void TestAnnotationStore::loadingClearsHistoryAndSeedsIds()
{
    AnnotationStore store;
    store.addItem(1, 0, stroke());
    QVERIFY(store.canUndo());

    SourceAnnotations loaded;
    FrameAnnotation frame;
    frame.frame = 9;
    AnnotationItem item = stroke();
    item.id = 41;
    frame.items.append(item);
    loaded.setFrame(frame);

    QSignalSpy reset(&store, &AnnotationStore::annotationsReset);
    QSignalSpy edited(&store, &AnnotationStore::annotationsEdited);
    store.setAnnotations(loaded);
    QCOMPARE(reset.count(), 1);
    QCOMPARE(edited.count(), 0); // a load is not a user edit
    QVERIFY(!store.canUndo());
    QCOMPARE(store.addItem(9, 0, stroke()), quint64(42)); // no id collision
}

void TestAnnotationStore::limitsAreEnforced()
{
    AnnotationStore store;
    AnnotationItem big = stroke();
    big.points.clear();
    for (int i = 0; i < AnnotationLimits::kMaxPointsPerItem; ++i) big.points.append({0.5f, 0.5f});
    QVERIFY(store.addItem(0, 0, big) != 0);
    big.points.append({0.5f, 0.5f});
    QCOMPARE(store.addItem(1, 0, big), quint64(0));

    AnnotationStore crowded;
    for (int i = 0; i < AnnotationLimits::kMaxItemsPerFrame; ++i)
        QVERIFY(crowded.addItem(2, 0, stroke()) != 0);
    QCOMPARE(crowded.addItem(2, 0, stroke()), quint64(0));
    QCOMPARE(crowded.frame(2).items.size(), AnnotationLimits::kMaxItemsPerFrame);
}

void TestAnnotationStore::trimDropsFramesBeyondEnd()
{
    SourceAnnotations set;
    for (int64_t f : {2, 50, 99}) {
        FrameAnnotation frame;
        frame.frame = f;
        frame.items = {stroke()};
        set.setFrame(frame);
    }
    QVERIFY(set.trimTo(50));
    QCOMPARE(set.annotatedFrames(), QVector<int64_t>({2, 50}));
    QVERIFY(!set.trimTo(50));
}

void TestAnnotationStore::signalsFireOncePerEdit()
{
    AnnotationStore store;
    QSignalSpy edited(&store, &AnnotationStore::annotationsEdited);
    QSignalSpy frames(&store, &AnnotationStore::frameChanged);
    store.addItem(3, 0, stroke());
    QCOMPARE(edited.count(), 1);
    QCOMPARE(frames.count(), 1);
    QCOMPARE(frames.at(0).at(0).toLongLong(), qint64(3));
    store.undo();
    QCOMPARE(edited.count(), 2);
    store.addItem(1, 0, stroke());
    store.addItem(2, 0, stroke());
    edited.clear();
    frames.clear();
    store.clearAll();
    QCOMPARE(edited.count(), 1); // one edit, though two frames changed
    QCOMPARE(frames.count(), 2);
}

QTEST_APPLESS_MAIN(TestAnnotationStore)
#include "tst_annotationstore.moc"
