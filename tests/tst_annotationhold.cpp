#include "annotation/AnnotationHold.h"

#include <QTest>

using namespace atk::annotation;

namespace {
FrameAnnotation drawn(int64_t frame)
{
    FrameAnnotation a;
    a.frame = frame;
    AnnotationItem item;
    item.id = quint64(frame + 1);
    item.tool = AnnotationTool::Pen;
    item.points = {{0.1f, 0.1f, 1, 0, 0}, {0.2f, 0.2f, 1, 0, 0}};
    a.items = {item};
    return a;
}

FrameAnnotation noteOnly(int64_t frame)
{
    FrameAnnotation a;
    a.frame = frame;
    a.notes = {{quint64(1000 + frame), QStringLiteral("note"), QString(), 0, false}};
    return a;
}
} // namespace

/// Held drawings: which drawing is on screen at a frame, and the spans the
/// timeline draws. Pure functions over SourceAnnotations.
class TestAnnotationHold : public QObject {
    Q_OBJECT

private slots:
    void nothingHeldWithoutDrawings();
    void drawingHoldsUntilTheNextOne();
    void notesOnlyFramesNeverHold();
    void spansRunToTheFrameBeforeTheNextDrawing();
    void spansStopAtTheLastFrame();
    void modeKeysRoundTrip();
};

void TestAnnotationHold::nothingHeldWithoutDrawings()
{
    const SourceAnnotations empty;
    QCOMPARE(heldDrawingFrame(empty, 0), int64_t(-1));
    QCOMPARE(heldDrawingFrame(empty, 50), int64_t(-1));
    QVERIFY(holdSpans(empty, 99).isEmpty());
}

void TestAnnotationHold::drawingHoldsUntilTheNextOne()
{
    SourceAnnotations set;
    set.setFrame(drawn(10));
    set.setFrame(drawn(20));
    QCOMPARE(heldDrawingFrame(set, 9), int64_t(-1)); // before any drawing
    QCOMPARE(heldDrawingFrame(set, 10), int64_t(10)); // its own frame
    QCOMPARE(heldDrawingFrame(set, 15), int64_t(10));
    QCOMPARE(heldDrawingFrame(set, 19), int64_t(10));
    QCOMPARE(heldDrawingFrame(set, 20), int64_t(20)); // the next drawing takes over
    QCOMPARE(heldDrawingFrame(set, 5000), int64_t(20));
    QCOMPARE(heldDrawingFrame(set, -1), int64_t(-1));
}

void TestAnnotationHold::notesOnlyFramesNeverHold()
{
    SourceAnnotations set;
    set.setFrame(drawn(10));
    set.setFrame(noteOnly(15));
    QCOMPARE(heldDrawingFrame(set, 15), int64_t(10));
    QCOMPARE(heldDrawingFrame(set, 18), int64_t(10));
    QCOMPARE(holdSpans(set, 30), QVector<HoldSpan>({{10, 30}}));
}

void TestAnnotationHold::spansRunToTheFrameBeforeTheNextDrawing()
{
    SourceAnnotations set;
    set.setFrame(drawn(0));
    set.setFrame(drawn(1));
    set.setFrame(drawn(12));
    QCOMPARE(holdSpans(set, 47), QVector<HoldSpan>({{0, 0}, {1, 11}, {12, 47}}));
}

void TestAnnotationHold::spansStopAtTheLastFrame()
{
    SourceAnnotations set;
    set.setFrame(drawn(40));
    set.setFrame(drawn(80)); // past the clip: ignored
    QCOMPARE(holdSpans(set, 59), QVector<HoldSpan>({{40, 59}}));
    QVERIFY(holdSpans(set, -1).isEmpty());
}

void TestAnnotationHold::modeKeysRoundTrip()
{
    for (const HoldMode mode : {HoldMode::Off, HoldMode::AddToCurrent, HoldMode::AddNew})
        QCOMPARE(holdModeFromKey(holdModeKey(mode)), mode);
    QCOMPARE(holdModeFromKey(QStringLiteral("nonsense")), HoldMode::Off);
    QCOMPARE(holdModeFromKey(QString(), HoldMode::AddNew), HoldMode::AddNew);
}

QTEST_GUILESS_MAIN(TestAnnotationHold)
#include "tst_annotationhold.moc"
