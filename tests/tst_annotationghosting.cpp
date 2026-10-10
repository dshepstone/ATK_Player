#include "annotation/AnnotationGhosting.h"

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

SourceAnnotations keys(std::initializer_list<int64_t> frames)
{
    SourceAnnotations set;
    for (int64_t f : frames) set.setFrame(drawn(f));
    return set;
}

QVector<int> offsets(const QVector<GhostFrame>& ghosts)
{
    QVector<int> result;
    for (const GhostFrame& g : ghosts) result.append(g.offset);
    return result;
}
} // namespace

class TestAnnotationGhosting : public QObject {
    Q_OBJECT
private slots:
    void disabledOrNoFrameGivesNothing();
    void framesModeUsesATimelineWindow();
    void drawingsModeCountsDrawnFrames();
    void farthestFirstWithFadeAndTint();
    void noteOnlyFramesAndTheCurrentFrameNeverGhost();
    void rangesAreClamped();
};

void TestAnnotationGhosting::disabledOrNoFrameGivesNothing()
{
    const SourceAnnotations set = keys({9, 11});
    GhostSettings s;
    QVERIFY(ghostFramesFor(set, 10, s).isEmpty()); // off by default
    s.enabled = true;
    QVERIFY(ghostFramesFor(set, -1, s).isEmpty());
    QVERIFY(ghostFramesFor(SourceAnnotations{}, 10, s).isEmpty());
}

void TestAnnotationGhosting::framesModeUsesATimelineWindow()
{
    const SourceAnnotations set = keys({2, 6, 8, 10, 12, 13, 20});
    GhostSettings s;
    s.enabled = true;
    s.before = 3;
    s.after = 3;
    // Frames 7..13 around 10: 8 before, 12 and 13 after. 6 and 20 are too far.
    QCOMPARE(offsets(ghostFramesFor(set, 10, s)), (QVector<int>{3, -2, 2}));
    s.before = 0;
    QCOMPARE(offsets(ghostFramesFor(set, 10, s)), (QVector<int>{3, 2}));
    s.before = 3;
    s.after = 0;
    QCOMPARE(offsets(ghostFramesFor(set, 10, s)), (QVector<int>{-2}));
}

void TestAnnotationGhosting::drawingsModeCountsDrawnFrames()
{
    const SourceAnnotations set = keys({2, 6, 8, 10, 12, 13, 20});
    GhostSettings s;
    s.enabled = true;
    s.mode = GhostSettings::Mode::Drawings;
    s.before = 2;
    s.after = 1;
    // The two nearest drawn frames before (8, 6) and the nearest after (12),
    // however far away they are.
    QCOMPARE(offsets(ghostFramesFor(set, 10, s)), (QVector<int>{-4, -2, 2}));
    s.after = 3;
    QCOMPARE(offsets(ghostFramesFor(set, 10, s)), (QVector<int>{10, -4, 3, -2, 2}));
}

void TestAnnotationGhosting::farthestFirstWithFadeAndTint()
{
    const SourceAnnotations set = keys({7, 8, 9, 11});
    GhostSettings s;
    s.enabled = true;
    s.before = 3;
    s.after = 3;
    const QVector<GhostFrame> ghosts = ghostFramesFor(set, 10, s);
    QCOMPARE(offsets(ghosts), (QVector<int>{-3, -2, -1, 1}));
    // Fades with distance, nearest strongest.
    QVERIFY(ghosts[0].opacity < ghosts[1].opacity);
    QVERIFY(ghosts[1].opacity < ghosts[2].opacity);
    QCOMPARE(ghosts[2].opacity, ghostOpacity(1, 3));
    QCOMPARE(ghosts[2].tint, ghostBeforeColor());
    QCOMPARE(ghosts[3].tint, ghostAfterColor());
    QCOMPARE(ghosts[2].frame.frame, int64_t(9));
    s.tint = false;
    for (const GhostFrame& g : ghostFramesFor(set, 10, s)) QVERIFY(!g.tint.isValid());
    QCOMPARE(ghostOpacity(1, 1), 0.55);
    QVERIFY(ghostOpacity(5, 5) > 0.1);
}

void TestAnnotationGhosting::noteOnlyFramesAndTheCurrentFrameNeverGhost()
{
    SourceAnnotations set = keys({10});
    set.setFrame(noteOnly(9));
    set.setFrame(noteOnly(11));
    GhostSettings s;
    s.enabled = true;
    QVERIFY(ghostFramesFor(set, 10, s).isEmpty());
    s.mode = GhostSettings::Mode::Drawings;
    QVERIFY(ghostFramesFor(set, 10, s).isEmpty());
}

void TestAnnotationGhosting::rangesAreClamped()
{
    SourceAnnotations set;
    for (int64_t f = 0; f <= 200; ++f) set.setFrame(drawn(f));
    GhostSettings s;
    s.enabled = true;
    s.before = 1000;
    s.after = -5;
    const QVector<GhostFrame> ghosts = ghostFramesFor(set, 100, s);
    QCOMPARE(ghosts.size(), GhostSettings::kMaxRange);
    QCOMPARE(ghosts.first().offset, -GhostSettings::kMaxRange);
}

QTEST_GUILESS_MAIN(TestAnnotationGhosting)
#include "tst_annotationghosting.moc"
