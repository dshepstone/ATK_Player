#include "annotation/AnnotationEraser.h"
#include "annotation/AnnotationRenderer.h"
#include "annotation/AnnotationStore.h"
#include "annotation/AnnotationTypes.h"

#include <QSet>
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
    void eraseDiscCutsOnlyWhatItCovers();
    void eraseDiscModesAndShapes();
    void storeEraseAssignsIdsAndGroupsUndo();
    void boxTextValidityAndTranslation();
    void replaceItemKeepsIdOrderAndUndo();
};

void TestAnnotationStore::toolNamesRoundTripAndRejectUnknown()
{
    for (AnnotationTool tool : {AnnotationTool::Pencil, AnnotationTool::Pen, AnnotationTool::Brush,
                                AnnotationTool::Marker, AnnotationTool::Eraser, AnnotationTool::Line,
                                AnnotationTool::Arrow, AnnotationTool::Rectangle,
                                AnnotationTool::Ellipse, AnnotationTool::Text, AnnotationTool::Select}) {
        AnnotationTool parsed = AnnotationTool::Text;
        QVERIFY2(toolFromName(toolName(tool), &parsed), qPrintable(toolName(tool)));
        QCOMPARE(parsed, tool);
    }
    AnnotationTool untouched = AnnotationTool::Pen;
    QVERIFY(!toolFromName(QStringLiteral("laser"), &untouched));
    QCOMPARE(untouched, AnnotationTool::Pen);
    QVERIFY(!isStoredTool(AnnotationTool::Eraser));
    QVERIFY(!isStoredTool(AnnotationTool::Select));
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

namespace {
AnnotationItem line(float y = 0.5f)
{
    AnnotationItem item;
    item.tool = AnnotationTool::Pen;
    item.size = 0.01; // 10 px of 1000
    for (int i = 0; i <= 40; ++i) item.points.append({0.1f + 0.02f * i, y, 0.5f + 0.01f * i, 0, 0});
    return item;
}
const QSizeF kImage(1000, 1000);
} // namespace

void TestAnnotationStore::eraseDiscCutsOnlyWhatItCovers()
{
    const AnnotationItem item = line();
    // A miss changes nothing.
    QVERIFY(!eraseDisc(item, kImage, {0.5, 0.2}, 20, EraserMode::Area).changed);

    // A cut in the middle leaves two pieces, and no remaining ink inside the disc.
    const EraseResult cut = eraseDisc(item, kImage, {0.5, 0.5}, 30, EraserMode::Area);
    QVERIFY(cut.changed);
    QCOMPARE(cut.pieces.size(), 2);
    const QRectF rect(QPointF(0, 0), kImage);
    for (const AnnotationItem& piece : cut.pieces) {
        QVERIFY(isValidItem(piece));
        QCOMPARE(piece.id, quint64(0));
        QCOMPARE(piece.tool, item.tool);
        for (const StrokePoint& p : piece.points) {
            const qreal d = std::hypot(p.x * 1000.0 - 500.0, p.y * 1000.0 - 500.0);
            QVERIFY(d - AnnotationRenderer::inkRadius(rect, piece, p.pressure) > 30.0 - 1e-6);
        }
    }
    // Pressure along the remaining pieces is preserved (it increases left to right).
    QVERIFY(cut.pieces[0].points.constFirst().pressure < cut.pieces[1].points.constLast().pressure);
    // The gap is small: the pieces still reach close to the disc.
    QVERIFY(cut.pieces[0].points.constLast().x > 0.45f);
    QVERIFY(cut.pieces[1].points.constFirst().x < 0.55f);

    // At an end, one piece remains; covering everything leaves none.
    QCOMPARE(eraseDisc(item, kImage, {0.1, 0.5}, 30, EraserMode::Area).pieces.size(), 1);
    const EraseResult all = eraseDisc(item, kImage, {0.5, 0.5}, 600, EraserMode::Area);
    QVERIFY(all.changed);
    QVERIFY(all.pieces.isEmpty());

    // The disc reaches the ink's edge, not just its centreline. At x = 0.5 the
    // pen's ink radius is about 3.7 px, so a 3 px disc 6 px above the centreline
    // touches the ink (but would miss the centreline itself).
    QVERIFY(eraseDisc(item, kImage, {0.5, 0.5 - 0.006}, 3, EraserMode::Area).changed);
    QVERIFY(!eraseDisc(item, kImage, {0.5, 0.5 - 0.008}, 3, EraserMode::Area).changed);

    // A stroke stored as just two far-apart points is still cut between them.
    AnnotationItem sparse = item;
    sparse.points = {item.points.constFirst(), item.points.constLast()};
    const EraseResult sparseCut = eraseDisc(sparse, kImage, {0.5, 0.5}, 10, EraserMode::Area);
    QVERIFY(sparseCut.changed);
    QCOMPARE(sparseCut.pieces.size(), 2);
}

void TestAnnotationStore::eraseDiscModesAndShapes()
{
    const EraseResult whole = eraseDisc(line(), kImage, {0.5, 0.5}, 5, EraserMode::WholeStroke);
    QVERIFY(whole.changed);
    QVERIFY(whole.pieces.isEmpty());

    AnnotationItem rect;
    rect.tool = AnnotationTool::Rectangle;
    rect.size = 0.004;
    rect.points = {{0.2f, 0.2f, 1, 0, 0}, {0.8f, 0.8f, 1, 0, 0}};
    QVERIFY(!eraseDisc(rect, kImage, {0.5, 0.5}, 20, EraserMode::Area).changed); // hollow centre
    const EraseResult edge = eraseDisc(rect, kImage, {0.2, 0.5}, 20, EraserMode::Area);
    QVERIFY(edge.changed);
    QVERIFY(edge.pieces.isEmpty()); // shapes go whole
}

void TestAnnotationStore::storeEraseAssignsIdsAndGroupsUndo()
{
    AnnotationStore store;
    store.addItem(3, 0, line(0.5f));
    store.addItem(3, 0, line(0.7f));
    const SourceAnnotations original = store.annotations();
    store.clearHistory();

    QVERIFY(!store.eraseDiscs(3, {{0.5, 0.1}}, 10, kImage, EraserMode::Area)); // miss
    QVERIFY(!store.eraseDiscs(9, {{0.5, 0.5}}, 10, kImage, EraserMode::Area)); // no such frame

    store.beginEditGroup();
    QVERIFY(store.eraseDiscs(3, {{0.5, 0.5}}, 15, kImage, EraserMode::Area)); // cut line 1
    QVERIFY(store.eraseDiscs(3, {{0.3, 0.7}, {0.31, 0.7}}, 15, kImage, EraserMode::Area)); // cut line 2
    QVERIFY(!store.canUndo()); // nothing recorded until the swipe ends
    store.endEditGroup();
    QCOMPARE(store.frame(3).items.size(), 4);
    QSet<quint64> ids;
    for (const auto& item : store.frame(3).items) {
        QVERIFY(item.id > 2);
        ids.insert(item.id);
    }
    QCOMPARE(ids.size(), 4);

    QVERIFY(store.undo()); // the whole swipe at once
    QCOMPARE(store.annotations(), original);
    QVERIFY(!store.canUndo());
    QVERIFY(store.redo());
    QCOMPARE(store.frame(3).items.size(), 4);

    // An empty group records nothing; undo closes an open group first.
    store.clearHistory();
    store.beginEditGroup();
    store.endEditGroup();
    QVERIFY(!store.canUndo());
    store.beginEditGroup();
    store.eraseDiscs(3, {{0.5, 0.5}}, 600, kImage, EraserMode::Area);
    QVERIFY(store.frame(3).isEmpty());
    QVERIFY(store.undo());
    QVERIFY(!store.inEditGroup());
    QCOMPARE(store.frame(3).items.size(), 4);
}

void TestAnnotationStore::boxTextValidityAndTranslation()
{
    AnnotationItem text;
    text.tool = AnnotationTool::Text;
    text.text = QStringLiteral("note");
    text.points = {{0.2f, 0.2f, 1, 0, 0}};
    QVERIFY(isValidItem(text));
    QVERIFY(!isBoxText(text));
    text.points.append({0.6f, 0.4f, 1, 0, 0});
    QVERIFY(isValidItem(text));
    QVERIFY(isBoxText(text));
    text.points[1].x = 0.1f; // a box must have width
    QVERIFY(!isValidItem(text));
    AnnotationItem selectItem = text;
    selectItem.tool = AnnotationTool::Select;
    QVERIFY(!isValidItem(selectItem));

    const AnnotationItem item = line(0.5f); // x 0.1..0.9
    const AnnotationItem moved = translatedItem(item, {0.05, -0.1});
    QVERIFY(std::abs(moved.points[0].x - 0.15f) < 1e-6f && std::abs(moved.points[0].y - 0.4f) < 1e-6f);
    QCOMPARE(moved.id, item.id);
    // Clamped as a whole: the shape is kept, only the move is limited.
    const AnnotationItem far = translatedItem(item, {0.5, 0.0});
    QVERIFY(std::abs(far.points.constLast().x - 1.0f) < 1e-6f);
    QVERIFY(std::abs((far.points.constLast().x - far.points.constFirst().x)
                     - (item.points.constLast().x - item.points.constFirst().x)) < 1e-5f);
    QVERIFY(isValidItem(far));
}

void TestAnnotationStore::replaceItemKeepsIdOrderAndUndo()
{
    AnnotationStore store;
    const quint64 a = store.addItem(4, 0, stroke(0.2f, 0.2f));
    const quint64 b = store.addItem(4, 0, stroke(0.5f, 0.5f));
    store.clearHistory();
    AnnotationItem moved = translatedItem(store.frame(4).items[0], {0.1, 0.1});
    QVERIFY(store.replaceItem(4, moved));
    QCOMPARE(store.frame(4).items[0].id, a); // still first, still the same id
    QCOMPARE(store.frame(4).items[1].id, b);
    QCOMPARE(store.frame(4).items[0], moved);
    QVERIFY(!store.replaceItem(4, moved)); // no change, no undo step
    AnnotationItem unknown = moved;
    unknown.id = 999;
    QVERIFY(!store.replaceItem(4, unknown));
    AnnotationItem invalid = moved;
    invalid.points.clear();
    QVERIFY(!store.replaceItem(4, invalid));
    QVERIFY(store.undo());
    QCOMPARE(store.frame(4).items[0].points, stroke(0.2f, 0.2f).points);
    QVERIFY(!store.canUndo());
}

QTEST_APPLESS_MAIN(TestAnnotationStore)
#include "tst_annotationstore.moc"
