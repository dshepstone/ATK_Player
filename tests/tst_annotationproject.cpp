#include "annotation/AnnotationTypes.h"
#include "annotation/BrushTip.h"
#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace atk;

namespace {
annotation::AnnotationItem makeItem(quint64 id, annotation::AnnotationTool tool, float x)
{
    annotation::AnnotationItem item;
    item.id = id;
    item.tool = tool;
    item.color = QColor(12, 200, 99, 180);
    item.size = 0.0125;
    item.opacity = 0.75;
    item.textureSeed = 4000000000u;
    if (tool == annotation::AnnotationTool::Text) {
        item.text = QString::fromUtf8("fix the nose \xE2\x80\x94 \xE9\xA3\x9F");
        item.points = {{x, 0.25f, 1.0f, 0.0f, 0.0f}};
    } else if (annotation::isShapeTool(tool)) {
        item.points = {{x, 0.1f, 1.0f, 0.0f, 0.0f}, {x + 0.2f, 0.4f, 1.0f, 0.0f, 0.0f}};
    } else {
        item.points = {{x, 0.1f, 0.25f, 0.0f, 0.0f},
                       {x + 0.05f, 0.15f, 0.6f, 12.5f, -3.25f},
                       {x + 0.1f, 0.2f, 1.0f, 0.0f, 0.0f}};
    }
    return item;
}

annotation::SourceAnnotations sampleAnnotations()
{
    annotation::SourceAnnotations set;
    annotation::FrameAnnotation a;
    a.frame = 0;
    a.mediaTimeUs = 0;
    a.items = {makeItem(1, annotation::AnnotationTool::Pen, 0.1f),
               makeItem(2, annotation::AnnotationTool::Text, 0.3f)};
    annotation::FrameAnnotation b;
    b.frame = 132;
    b.mediaTimeUs = 5505505;
    b.items = {makeItem(3, annotation::AnnotationTool::Ellipse, 0.2f),
               makeItem(4, annotation::AnnotationTool::Pencil, 0.4f)};
    set.setFrame(a);
    set.setFrame(b);
    return set;
}

struct Fixture {
    QTemporaryDir directory;
    QString mediaPath;
    QString projectPath;
    Fixture()
    {
        mediaPath = directory.filePath(QStringLiteral("clip.mp4"));
        QFile media(mediaPath);
        if (media.open(QIODevice::WriteOnly)) media.write("fixture");
        projectPath = directory.filePath(QStringLiteral("review.atkproj"));
    }
};

QJsonObject readProject(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

void writeProject(const QString& path, const QJsonObject& root)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
}

/// Rewrites the first source's annotations array and returns the load result.
project::SerializerResult loadWithAnnotations(const Fixture& fx, const QJsonValue& annotations)
{
    QJsonObject root = readProject(fx.projectPath);
    QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
    QJsonObject source = sources.at(0).toObject();
    QJsonObject review = source.value(QStringLiteral("review")).toObject();
    review.insert(QStringLiteral("annotations"), annotations);
    source.insert(QStringLiteral("review"), review);
    sources[0] = source;
    root.insert(QStringLiteral("sources"), sources);
    writeProject(fx.projectPath, root);
    project::Project loaded;
    return project::ProjectSerializer::load(loaded, fx.projectPath);
}

/// A points array holding one point. Built by append: MSVC reads
/// QJsonArray{QJsonArray{...}} as a copy, not as a nested array.
QJsonArray onePoint(const QJsonArray& point)
{
    QJsonArray points;
    points.append(point);
    return points;
}

QJsonObject validItemJson()
{
    return {{QStringLiteral("id"), QStringLiteral("1")}, {QStringLiteral("tool"), QStringLiteral("pen")},
            {QStringLiteral("color"), QStringLiteral("#ffff0000")}, {QStringLiteral("size"), 0.01},
            {QStringLiteral("opacity"), 1.0}, {QStringLiteral("seed"), 0},
            {QStringLiteral("points"), onePoint(QJsonArray{0.1, 0.2, 0.5})}};
}

QJsonArray oneFrame(const QJsonObject& item, qint64 frame = 5)
{
    return {QJsonObject{{QStringLiteral("frame"), static_cast<double>(frame)},
                        {QStringLiteral("mediaTimeUs"), 0},
                        {QStringLiteral("items"), QJsonArray{item}}}};
}
} // namespace

class TestAnnotationProject : public QObject {
    Q_OBJECT
private slots:
    void roundTripPreservesEveryField();
    void projectsWithoutAnnotationsOmitTheKeyAndStayVersionOne();
    void versionOneFilesWithoutAnnotationsStillLoad();
    void rejectsMalformedAnnotationData_data();
    void rejectsMalformedAnnotationData();
    void rejectedLoadLeavesExistingProjectUntouched();
    void setAnnotationsMarksModifiedAndEmitsSignal();
    void unknownSourceIsRejected();
    void annotationsAloneTriggerSavePrompt();
    void annotationsFollowSourceIdentityAcrossReorder();
    void relinkTrimsAnnotationsBeyondNewExtent();
    void usedBrushTipsTravelWithTheProject();
    void notesRoundTripIncludingNoteOnlyFrames();
    void projectsWithDrawingsAreVersionTwoSoOlderBuildsRefuseThem();
    void rotatedShapesRoundTripAsVersionFour();
    void nullAnnotationsAndHugeIdsAreRejected();
};

void TestAnnotationProject::roundTripPreservesEveryField()
{
    Fixture fx;
    QVERIFY(fx.directory.isValid());
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    const QUuid id = original.entries()[0].id;
    const auto annotations = sampleAnnotations();
    QVERIFY(original.setAnnotations(id, annotations));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);

    project::Project loaded;
    const auto result = project::ProjectSerializer::load(loaded, fx.projectPath);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(loaded.entries()[0].annotations, annotations); // exact, incl. float pressure/tilt
    QCOMPARE(loaded.entries()[0].annotations.maxItemId(), quint64(4));
    QVERIFY(!loaded.isModified());
}

void TestAnnotationProject::projectsWithoutAnnotationsOmitTheKeyAndStayVersionOne()
{
    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);
    const QJsonObject root = readProject(fx.projectPath);
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
    const QJsonObject review = root.value(QStringLiteral("sources")).toArray().at(0).toObject()
                                   .value(QStringLiteral("review")).toObject();
    QVERIFY(!review.contains(QStringLiteral("annotations")));
}

void TestAnnotationProject::versionOneFilesWithoutAnnotationsStillLoad()
{
    Fixture fx;
    const QByteArray json = R"({"format":"ATKProject","version":1,"name":"Old","sources":[
        {"id":"6f1c2a52-6a8f-4d4e-9b8e-0d1f0e2a3b4c","path":"clip.mp4","review":{"bookmarks":[]}}]})";
    QFile file(fx.projectPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json);
    file.close();
    project::Project loaded;
    QVERIFY(project::ProjectSerializer::load(loaded, fx.projectPath).ok);
    QVERIFY(loaded.entries()[0].annotations.isEmpty());
}

void TestAnnotationProject::rejectsMalformedAnnotationData_data()
{
    QTest::addColumn<QJsonValue>("annotations");

    QJsonObject badTool = validItemJson();
    badTool[QStringLiteral("tool")] = QStringLiteral("laser");
    QJsonObject eraser = validItemJson();
    eraser[QStringLiteral("tool")] = QStringLiteral("eraser");
    QJsonObject badColour = validItemJson();
    badColour[QStringLiteral("color")] = QStringLiteral("not-a-colour");
    QJsonObject offImage = validItemJson();
    offImage[QStringLiteral("points")] = onePoint(QJsonArray{1.5, 0.2, 0.5});
    QJsonObject badPressure = validItemJson();
    badPressure[QStringLiteral("points")] = onePoint(QJsonArray{0.1, 0.2, -0.5});
    QJsonObject shortPoint = validItemJson();
    shortPoint[QStringLiteral("points")] = onePoint(QJsonArray{0.1, 0.2});
    QJsonObject stringPoint = validItemJson();
    stringPoint[QStringLiteral("points")] = onePoint(QJsonArray{QStringLiteral("a"), 0.2, 0.5});
    QJsonObject noPoints = validItemJson();
    noPoints[QStringLiteral("points")] = QJsonArray{};
    QJsonObject zeroId = validItemJson();
    zeroId[QStringLiteral("id")] = QStringLiteral("0");
    QJsonObject badId = validItemJson();
    badId[QStringLiteral("id")] = QStringLiteral("abc");
    QJsonObject hugeSize = validItemJson();
    hugeSize[QStringLiteral("size")] = 5.0;
    QJsonObject negativeSeed = validItemJson();
    negativeSeed[QStringLiteral("seed")] = -1;
    QJsonObject textOnStroke = validItemJson();
    textOnStroke[QStringLiteral("text")] = QStringLiteral("stray");
    QJsonObject rotatedStroke = validItemJson(); // only rectangles and ellipses turn
    rotatedStroke[QStringLiteral("rotation")] = 30.0;
    QJsonObject rectangle = validItemJson();
    rectangle[QStringLiteral("tool")] = QStringLiteral("rectangle");
    QJsonArray corners{QJsonArray{0.1, 0.2, 1.0}, QJsonArray{0.4, 0.5, 1.0}};
    rectangle[QStringLiteral("points")] = corners;
    QJsonObject textRotation = rectangle;
    textRotation[QStringLiteral("rotation")] = QStringLiteral("30");
    QJsonObject wideRotation = rectangle;
    wideRotation[QStringLiteral("rotation")] = 270.0;

    QTest::newRow("not-an-array") << QJsonValue(QStringLiteral("nope"));
    QTest::newRow("frame-not-object") << QJsonValue(QJsonArray{1});
    QTest::newRow("negative-frame") << QJsonValue(oneFrame(validItemJson(), -1));
    QTest::newRow("fractional-frame") << QJsonValue(oneFrame(validItemJson(), 0)); // replaced below
    QTest::newRow("unknown-tool") << QJsonValue(oneFrame(badTool));
    QTest::newRow("eraser-stored") << QJsonValue(oneFrame(eraser));
    QTest::newRow("bad-colour") << QJsonValue(oneFrame(badColour));
    QTest::newRow("point-off-image") << QJsonValue(oneFrame(offImage));
    QTest::newRow("negative-pressure") << QJsonValue(oneFrame(badPressure));
    QTest::newRow("short-point") << QJsonValue(oneFrame(shortPoint));
    QTest::newRow("non-numeric-point") << QJsonValue(oneFrame(stringPoint));
    QTest::newRow("no-points") << QJsonValue(oneFrame(noPoints));
    QTest::newRow("zero-id") << QJsonValue(oneFrame(zeroId));
    QTest::newRow("non-numeric-id") << QJsonValue(oneFrame(badId));
    QTest::newRow("huge-size") << QJsonValue(oneFrame(hugeSize));
    QTest::newRow("negative-seed") << QJsonValue(oneFrame(negativeSeed));
    QTest::newRow("rotated-stroke") << QJsonValue(oneFrame(rotatedStroke));
    QTest::newRow("non-numeric-rotation") << QJsonValue(oneFrame(textRotation));
    QTest::newRow("rotation-out-of-range") << QJsonValue(oneFrame(wideRotation));
    QTest::newRow("text-on-stroke") << QJsonValue(oneFrame(textOnStroke));

    QJsonArray duplicateFrame = oneFrame(validItemJson());
    duplicateFrame.append(oneFrame(validItemJson()).at(0));
    QTest::newRow("duplicate-frame") << QJsonValue(duplicateFrame);

    QJsonObject second = validItemJson();
    QJsonArray duplicateIds = oneFrame(validItemJson(), 5);
    duplicateIds.append(oneFrame(second, 6).at(0)); // same id "1" on another frame
    QTest::newRow("duplicate-item-id") << QJsonValue(duplicateIds);

    QJsonArray emptyItems{QJsonObject{{QStringLiteral("frame"), 3}, {QStringLiteral("items"), QJsonArray{}}}};
    QTest::newRow("frame-without-items") << QJsonValue(emptyItems);
}

void TestAnnotationProject::rejectsMalformedAnnotationData()
{
    QFETCH(QJsonValue, annotations);
    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);

    // "fractional-frame" row: patch in 1.5 for the frame index.
    if (QTest::currentDataTag() == QByteArray("fractional-frame")) {
        QJsonArray frames = annotations.toArray();
        QJsonObject frame = frames.at(0).toObject();
        frame[QStringLiteral("frame")] = 1.5;
        frames[0] = frame;
        annotations = frames;
    }
    const auto result = loadWithAnnotations(fx, annotations);
    QVERIFY2(!result.ok, "malformed annotations must reject the project");
    QVERIFY(result.errorMessage.contains(QStringLiteral("annotation"), Qt::CaseInsensitive));
}

void TestAnnotationProject::rejectedLoadLeavesExistingProjectUntouched()
{
    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);
    QVERIFY(!loadWithAnnotations(fx, QJsonValue(QStringLiteral("bad"))).ok);

    project::Project live;
    live.setName(QStringLiteral("Live"));
    live.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    QVERIFY(live.setAnnotations(live.entries()[0].id, sampleAnnotations()));
    const auto before = live.entries()[0].annotations;
    QVERIFY(!project::ProjectSerializer::load(live, fx.projectPath).ok);
    QCOMPARE(live.name(), QStringLiteral("Live"));
    QCOMPARE(live.entries()[0].annotations, before);
}

void TestAnnotationProject::setAnnotationsMarksModifiedAndEmitsSignal()
{
    Fixture fx;
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    const QUuid id = project.entries()[0].id;
    project.setModified(false);
    QSignalSpy changed(&project, &project::Project::annotationsChanged);
    QVERIFY(project.setAnnotations(id, sampleAnnotations()));
    QVERIFY(project.isModified());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toUuid(), id);
}

void TestAnnotationProject::unknownSourceIsRejected()
{
    project::Project project;
    project.setModified(false);
    QSignalSpy changed(&project, &project::Project::annotationsChanged);
    QVERIFY(!project.setAnnotations(QUuid::createUuid(), sampleAnnotations()));
    QVERIFY(!project.isModified());
    QCOMPARE(changed.count(), 0);
}

void TestAnnotationProject::annotationsAloneTriggerSavePrompt()
{
    Fixture fx;
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    project.setModified(false);
    QVERIFY(!project.needsSavePrompt());
    QVERIFY(project.setAnnotations(project.entries()[0].id, sampleAnnotations()));
    QVERIFY(project.needsSavePrompt()); // untitled, one source, drawings worth keeping
}

void TestAnnotationProject::annotationsFollowSourceIdentityAcrossReorder()
{
    Fixture fx;
    const QString second = fx.directory.filePath(QStringLiteral("b.mp4"));
    QFile file(second);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("b");
    file.close();
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    project.addSource(std::make_shared<media::MediaSource>(second));
    const QUuid firstId = project.entries()[0].id;
    QVERIFY(project.setAnnotations(firstId, sampleAnnotations()));
    project.moveSource(0, 1);
    const int index = project.indexForId(firstId);
    QCOMPARE(index, 1);
    QCOMPARE(project.entries()[index].annotations, sampleAnnotations());
    QVERIFY(project.entries()[0].annotations.isEmpty());
}

void TestAnnotationProject::relinkTrimsAnnotationsBeyondNewExtent()
{
    Fixture fx;
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    const QUuid id = project.entries()[0].id;
    QVERIFY(project.setAnnotations(id, sampleAnnotations())); // frames 0 and 132
    QVERIFY(project.relinkSource(id, std::make_shared<media::MediaSource>(fx.mediaPath), 100));
    QCOMPARE(project.entries()[0].annotations.annotatedFrames(), QVector<int64_t>({0}));
    QVERIFY(project.relinkSource(id, std::make_shared<media::MediaSource>(fx.mediaPath), 500));
    QCOMPARE(project.entries()[0].annotations.annotatedFrames(), QVector<int64_t>({0}));
}

void TestAnnotationProject::usedBrushTipsTravelWithTheProject()
{
    annotation::BrushTip tip;
    tip.id = QStringLiteral("abr-embedded-test");
    tip.name = QStringLiteral("Charcoal");
    tip.spacing = 0.4;
    tip.angle = 15;
    tip.mask = QImage(9, 7, QImage::Format_Grayscale8);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x) tip.mask.scanLine(y)[x] = uchar((x * 31 + y * 17) % 256);
    annotation::BrushTipRegistry::instance().add(tip);

    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    annotation::SourceAnnotations set;
    annotation::FrameAnnotation frame;
    frame.frame = 4;
    annotation::AnnotationItem stamp = makeItem(1, annotation::AnnotationTool::Pen, 0.2f);
    stamp.tool = annotation::AnnotationTool::Stamp;
    stamp.brushId = tip.id;
    frame.items = {stamp};
    set.setFrame(frame);
    QVERIFY(original.setAnnotations(original.entries()[0].id, set));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);

    const QJsonObject root = readProject(fx.projectPath);
    const QJsonArray tips = root.value(QStringLiteral("brushTips")).toArray();
    QCOMPARE(tips.size(), 1);
    QCOMPARE(tips[0].toObject().value(QStringLiteral("id")).toString(), tip.id);

    // A machine without the brush set: the project brings the tip with it.
    annotation::BrushTipRegistry::instance().clear();
    project::Project loaded;
    const auto result = project::ProjectSerializer::load(loaded, fx.projectPath);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(loaded.entries()[0].annotations, set); // brushId kept on the item
    const annotation::BrushTip restored = annotation::BrushTipRegistry::instance().find(tip.id);
    QVERIFY(restored.isValid());
    QCOMPARE(restored.name, tip.name);
    QCOMPARE(restored.spacing, tip.spacing);
    QCOMPARE(restored.mask.convertToFormat(QImage::Format_Grayscale8), tip.mask);

    // A damaged embedded tip is skipped, not fatal.
    QJsonObject damaged = root;
    QJsonArray badTips;
    badTips.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("broken")}, {QStringLiteral("png"), QStringLiteral("!!!")}});
    damaged.insert(QStringLiteral("brushTips"), badTips);
    writeProject(fx.projectPath, damaged);
    project::Project again;
    QVERIFY(project::ProjectSerializer::load(again, fx.projectPath).ok);
    QVERIFY(!annotation::BrushTipRegistry::instance().contains(QStringLiteral("broken")));

    // Projects without custom brushes do not gain the key.
    project::Project plain;
    plain.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    QVERIFY(project::ProjectSerializer::save(plain, fx.projectPath).ok);
    QVERIFY(!readProject(fx.projectPath).contains(QStringLiteral("brushTips")));
}

void TestAnnotationProject::notesRoundTripIncludingNoteOnlyFrames()
{
    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    annotation::SourceAnnotations set = sampleAnnotations(); // drawings on frames 0 and 132
    annotation::FrameAnnotation withNote = set.frame(132);
    withNote.notes = {{50, QStringLiteral("fix the nose\nand chin"), QStringLiteral("David"), 1700000000000, false},
                      {51, QStringLiteral("better"), QStringLiteral("Selena"), 1700000005000, true}};
    set.setFrame(withNote);
    annotation::FrameAnnotation noteOnly;
    noteOnly.frame = 60;
    noteOnly.notes = {{52, QStringLiteral("timing feels slow here"), QString(), 0, false}};
    set.setFrame(noteOnly);
    QVERIFY(original.setAnnotations(original.entries()[0].id, set));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);

    project::Project loaded;
    const auto result = project::ProjectSerializer::load(loaded, fx.projectPath);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(loaded.entries()[0].annotations, set);
    QCOMPARE(loaded.entries()[0].annotations.annotatedFrames(), QVector<int64_t>({0, 60, 132}));

    // A note id clashing with a drawing id, or a blank note, rejects the file.
    for (const QString& bad : {QStringLiteral("dup"), QStringLiteral("blank")}) {
        QJsonObject root = readProject(fx.projectPath);
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        QJsonObject source = sources[0].toObject();
        QJsonObject review = source.value(QStringLiteral("review")).toObject();
        QJsonArray frames = review.value(QStringLiteral("annotations")).toArray();
        for (int i = 0; i < frames.size(); ++i) {
            QJsonObject f = frames[i].toObject();
            if (f.value(QStringLiteral("frame")).toInt() != 60) continue;
            QJsonArray notes = f.value(QStringLiteral("notes")).toArray();
            QJsonObject n = notes[0].toObject();
            if (bad == QStringLiteral("dup")) n[QStringLiteral("id")] = QStringLiteral("1");
            else n[QStringLiteral("text")] = QStringLiteral("  ");
            notes[0] = n;
            f[QStringLiteral("notes")] = notes;
            frames[i] = f;
        }
        review[QStringLiteral("annotations")] = frames;
        source[QStringLiteral("review")] = review;
        sources[0] = source;
        root[QStringLiteral("sources")] = sources;
        Fixture other;
        writeProject(other.projectPath, root);
        project::Project rejected;
        QVERIFY2(!project::ProjectSerializer::load(rejected, other.projectPath).ok, qPrintable(bad));
    }
}

void TestAnnotationProject::projectsWithDrawingsAreVersionTwoSoOlderBuildsRefuseThem()
{
    // An older build ignores fields it does not know and would drop the
    // drawings on its next save; version 2 makes it refuse the file instead
    // ("created by a newer version"), so the drawings are never lost.
    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    QVERIFY(original.setAnnotations(original.entries()[0].id, sampleAnnotations()));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);
    QJsonObject root = readProject(fx.projectPath);
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
    project::Project loaded;
    QVERIFY(project::ProjectSerializer::load(loaded, fx.projectPath).ok);
    QCOMPARE(loaded.entries()[0].annotations, sampleAnnotations());

    // Files written as version 1 by development builds, drawings included,
    // still load.
    root.insert(QStringLiteral("version"), 1);
    writeProject(fx.projectPath, root);
    project::Project fromDevBuild;
    QVERIFY(project::ProjectSerializer::load(fromDevBuild, fx.projectPath).ok);
    QCOMPARE(fromDevBuild.entries()[0].annotations, sampleAnnotations());

    // Anything newer than this build is refused, not half-read. (Version 4,
    // rotated shapes, is the newest this build reads.)
    root.insert(QStringLiteral("version"), 5);
    writeProject(fx.projectPath, root);
    project::Project newer;
    const auto result = project::ProjectSerializer::load(newer, fx.projectPath);
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("newer")));
}

void TestAnnotationProject::rotatedShapesRoundTripAsVersionFour()
{
    // A version 3 reader would show a turned shape upright and save it that
    // way, so a project holding one is version 4; without one it stays lower.
    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    annotation::SourceAnnotations annotations = sampleAnnotations();
    annotation::FrameAnnotation frame = annotations.frame(132);
    frame.items[0].rotation = -37.5; // the ellipse
    annotations.setFrame(frame);
    QVERIFY(original.setAnnotations(original.entries()[0].id, annotations));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);
    QCOMPARE(readProject(fx.projectPath).value(QStringLiteral("version")).toInt(), 4);

    project::Project loaded;
    const auto result = project::ProjectSerializer::load(loaded, fx.projectPath);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(loaded.entries()[0].annotations, annotations);
    QCOMPARE(loaded.entries()[0].annotations.frame(132).items[0].rotation, -37.5);

    // Turned back upright, the key is dropped and the version falls back.
    frame.items[0].rotation = 0.0;
    annotations.setFrame(frame);
    QVERIFY(original.setAnnotations(original.entries()[0].id, annotations));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);
    const QJsonObject root = readProject(fx.projectPath);
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
    QVERIFY(!QJsonDocument(root).toJson().contains("rotation"));
}

void TestAnnotationProject::nullAnnotationsAndHugeIdsAreRejected()
{
    Fixture fx;
    project::Project original;
    original.addSource(std::make_shared<media::MediaSource>(fx.mediaPath));
    QVERIFY(project::ProjectSerializer::save(original, fx.projectPath).ok);
    // The writer never stores null: it means damage, not "no drawings".
    QVERIFY(!loadWithAnnotations(fx, QJsonValue::Null).ok);
    // Ids so large that the next allocation could wrap to the reserved 0.
    QJsonObject item = validItemJson();
    item.insert(QStringLiteral("id"), QString::number(std::numeric_limits<quint64>::max()));
    QVERIFY(!loadWithAnnotations(fx, oneFrame(item)).ok);
    item.insert(QStringLiteral("id"), QStringLiteral("9007199254740992")); // 2^53: the largest allowed
    const auto largest = loadWithAnnotations(fx, oneFrame(item));
    QVERIFY2(largest.ok, qPrintable(largest.errorMessage));
}

QTEST_MAIN(TestAnnotationProject)
#include "tst_annotationproject.moc"
