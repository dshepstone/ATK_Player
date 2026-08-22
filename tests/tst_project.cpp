#include "media/MediaSource.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace atk;

class TestProject : public QObject {
    Q_OBJECT
private slots:
    void stableIdentitySurvivesReorder();
    void roundTripReviewStateAndRelativePath();
    void missingMediaRemainsInProject();
    void rejectsMalformedAndUnsupportedFilesWithoutMutation();
    void removeCurrentChoosesNoImplicitIdentityAndDuplicatesAreAllowed();
    void relinkPreservesIdentityAndValidReviewState();
    void probeResultsRouteByStableIdentityAndToken();
    void loadsOriginalVersionOneSchemaWithoutDerivedMetadata();
    void saveAsPersistsDestinationNameWithoutMutatingSource();
    void failedSaveAsLeavesProjectIdentityAndDirtyStateUntouched();
    void normalSavePreservesExistingProjectName();
};

void TestProject::saveAsPersistsDestinationNameWithoutMutatingSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    project::Project source;
    source.setName(QStringLiteral("Untitled"));
    source.setModified(true);
    const QString path = directory.filePath(QStringLiteral("MyProject.atkproj"));

    const auto result = project::ProjectSerializer::saveAs(source, path);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(source.name(), QStringLiteral("Untitled"));
    QCOMPARE(source.filePath(), QString());
    QVERIFY(source.isModified());

    project::Project loaded;
    QVERIFY(project::ProjectSerializer::load(loaded, path).ok);
    QCOMPARE(loaded.name(), QStringLiteral("MyProject"));
    QCOMPARE(loaded.filePath(), QFileInfo(path).absoluteFilePath());
    QVERIFY(!loaded.isModified());
}

void TestProject::failedSaveAsLeavesProjectIdentityAndDirtyStateUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    project::Project source;
    source.setName(QStringLiteral("Original"));
    source.setFilePath(directory.filePath(QStringLiteral("Original.atkproj")));
    source.setModified(true);
    const QString originalPath = source.filePath();

    const auto result = project::ProjectSerializer::saveAs(source, directory.path());
    QVERIFY(!result.ok);
    QCOMPARE(source.name(), QStringLiteral("Original"));
    QCOMPARE(source.filePath(), originalPath);
    QVERIFY(source.isModified());
}

void TestProject::normalSavePreservesExistingProjectName()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    project::Project source;
    source.setName(QStringLiteral("Editorial Name"));
    const QString path = directory.filePath(QStringLiteral("DifferentFilename.atkproj"));
    QVERIFY(project::ProjectSerializer::save(source, path).ok);
    project::Project loaded;
    QVERIFY(project::ProjectSerializer::load(loaded, path).ok);
    QCOMPARE(loaded.name(), QStringLiteral("Editorial Name"));
}

void TestProject::loadsOriginalVersionOneSchemaWithoutDerivedMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString mediaPath = directory.filePath(QStringLiteral("clip.mp4"));
    QFile mediaFile(mediaPath);
    QVERIFY(mediaFile.open(QIODevice::WriteOnly));
    mediaFile.write("fixture");
    mediaFile.close();

    const QUuid sourceId = QUuid::createUuid();
    const QString projectPath = directory.filePath(QStringLiteral("legacy.atkproj"));
    QFile projectFile(projectPath);
    QVERIFY(projectFile.open(QIODevice::WriteOnly));
    projectFile.write(QStringLiteral(R"({
        "format":"ATKProject",
        "version":1,
        "name":"Legacy Review",
        "currentSourceId":"%1",
        "sources":[{
            "id":"%1",
            "path":"clip.mp4",
            "displayName":"Legacy Clip",
            "frameOffset":0,
            "playbackRange":{"enabled":true,"startFrame":3,"endFrame":12},
            "bookmarks":[]
        }]
    })").arg(sourceId.toString(QUuid::WithoutBraces)).toUtf8());
    projectFile.close();

    project::Project loaded;
    const auto result = project::ProjectSerializer::load(loaded, projectPath);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(loaded.name(), QStringLiteral("Legacy Review"));
    QCOMPARE(loaded.entries().size(), 1);
    QCOMPARE(loaded.entries()[0].id, sourceId);
    QCOMPARE(loaded.currentSourceId(), sourceId);
    QCOMPARE(loaded.entries()[0].availability, project::SourceAvailability::Unknown);
    QVERIFY(!loaded.entries()[0].source->metadata().isValid());
    QVERIFY(!loaded.isModified());
}

void TestProject::probeResultsRouteByStableIdentityAndToken()
{
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(QStringLiteral("a.mp4")));
    project.addSource(std::make_shared<media::MediaSource>(QStringLiteral("b.mp4")));
    const QUuid a = project.entries()[0].id;
    const QString path = project.entries()[0].source->filePath();
    project.setModified(false);
    QVERIFY(project.beginProbe(a, path, 10));
    project.moveSource(0, 1);
    project.setModified(false);
    media::MediaMetadata metadata; metadata.hasVideo = true; metadata.resolution = {1920, 1080};
    metadata.frameRate = media::FrameRate::fromInteger(24); metadata.durationUs = 1'000'000;
    QVERIFY(project.applyProbeResult(a, path, 10, metadata, {}, false));
    QCOMPARE(project.entries()[project.indexForId(a)].source->metadata().resolution, QSize(1920, 1080));
    QVERIFY(!project.isModified());
    QVERIFY(project.beginProbe(a, path, 11));
    QVERIFY(!project.applyProbeResult(a, path, 10, metadata, {}, false));
    project.setModified(false);
    QVERIFY(project.applyProbeResult(a, path, 11, {}, QStringLiteral("unsupported media"), false));
    QCOMPARE(project.entries()[project.indexForId(a)].availability, project::SourceAvailability::Error);
    QCOMPARE(project.entries()[project.indexForId(a)].source->filePath(), path);
    QVERIFY(!project.isModified());
    QVERIFY(project.beginProbe(a, path, 12));
    const int index = project.indexForId(a); project.removeSourceAt(index); project.setModified(false);
    QVERIFY(!project.applyProbeResult(a, path, 12, metadata, {}, false));
    QVERIFY(!project.isModified());

    project::SourceEntry replacementEntry;
    replacementEntry.id = a;
    replacementEntry.source = std::make_shared<media::MediaSource>(path);
    project.replace(QStringLiteral("Replacement project"), {}, {replacementEntry}, a);
    QVERIFY(!project.applyProbeResult(a, path, 12, metadata, {}, false));
    QVERIFY(!project.entries()[0].source->metadata().isValid());
    QVERIFY(!project.isModified());
}

void TestProject::relinkPreservesIdentityAndValidReviewState()
{
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(QStringLiteral("missing.mp4")));
    auto& entry = project.mutableEntries()[0]; entry.missing = true; entry.playbackRange = {10, 80, true};
    timeline::Bookmark valid; valid.id = 7; valid.frame = 20; valid.endFrame = 20;
    timeline::Bookmark invalid; invalid.id = 8; invalid.frame = 70; invalid.endFrame = 70;
    timeline::Bookmark partial; partial.id = 9; partial.type = timeline::BookmarkType::Range;
    partial.frame = 40; partial.endFrame = 70;
    timeline::Bookmark beyond; beyond.id = 10; beyond.type = timeline::BookmarkType::Range;
    beyond.frame = 60; beyond.endFrame = 80;
    entry.bookmarks = {valid, invalid, partial, beyond}; const QUuid id = entry.id;
    project.setModified(false);
    QVERIFY(project.relinkSource(id, std::make_shared<media::MediaSource>(QStringLiteral("replacement.mp4")), 50));
    QCOMPARE(project.entries()[0].id, id);
    partial.endFrame = 49;
    QCOMPARE(project.entries()[0].bookmarks, QVector<timeline::Bookmark>({valid, partial}));
    QCOMPARE(project.entries()[0].playbackRange, timeline::PlaybackRange({10, 49, true}));
    QVERIFY(project.isModified());
}

void TestProject::removeCurrentChoosesNoImplicitIdentityAndDuplicatesAreAllowed()
{
    project::Project project;
    const auto path = QStringLiteral("same.mp4");
    project.addSource(std::make_shared<media::MediaSource>(path));
    project.addSource(std::make_shared<media::MediaSource>(path));
    QCOMPARE(project.entries().size(), 2);
    QVERIFY(project.entries()[0].id != project.entries()[1].id);
    project.setActiveIndex(0);
    project.removeSourceAt(0);
    QCOMPARE(project.entries().size(), 1);
    QCOMPARE(project.activeIndex(), -1);
}

void TestProject::stableIdentitySurvivesReorder()
{
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(QStringLiteral("a.mp4")));
    project.addSource(std::make_shared<media::MediaSource>(QStringLiteral("b.mp4")));
    project.setActiveIndex(0);
    const QUuid id = project.currentSourceId();
    project.moveSource(0, 1);
    QCOMPARE(project.currentSourceId(), id);
    QCOMPARE(project.activeIndex(), 1);
    QVERIFY(project.isModified());
}

void TestProject::roundTripReviewStateAndRelativePath()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QDir root(directory.path()); QVERIFY(root.mkpath(QStringLiteral("clips")));
    const QString mediaPath = root.filePath(QStringLiteral("clips/a.mp4"));
    QFile media(mediaPath); QVERIFY(media.open(QIODevice::WriteOnly)); media.write("fixture"); media.close();
    const QString projectPath = root.filePath(QStringLiteral("review.atkproj"));

    project::Project original;
    original.setName(QStringLiteral("Review"));
    original.addSource(std::make_shared<media::MediaSource>(mediaPath));
    auto& entry = original.mutableEntries()[0];
    entry.playbackRange = {12, 96, true};
    timeline::Bookmark point; point.id = 41; point.frame = 24; point.endFrame = 24;
    point.name = QStringLiteral("Contact"); point.note = QStringLiteral("Check foot"); point.colorIndex = 2;
    timeline::Bookmark range; range.id = 42; range.type = timeline::BookmarkType::Range;
    range.frame = 40; range.endFrame = 52; range.name = QStringLiteral("Arc");
    entry.bookmarks = {point, range};
    const QUuid sourceId = entry.id;
    QVERIFY(project::ProjectSerializer::save(original, projectPath).ok);

    QFile saved(projectPath); QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto rootJson = QJsonDocument::fromJson(saved.readAll()).object();
    QCOMPARE(rootJson.value(QStringLiteral("format")).toString(), QStringLiteral("ATKProject"));
    QCOMPARE(rootJson.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(!rootJson.value(QStringLiteral("sources")).toArray().first().toObject()
                 .value(QStringLiteral("path")).toString().contains(QLatin1Char(':')));

    project::Project loaded;
    QVERIFY(project::ProjectSerializer::load(loaded, projectPath).ok);
    QCOMPARE(loaded.entries().size(), 1);
    QCOMPARE(loaded.entries()[0].id, sourceId);
    QCOMPARE(loaded.entries()[0].playbackRange, timeline::PlaybackRange({12, 96, true}));
    QCOMPARE(loaded.entries()[0].bookmarks, QVector<timeline::Bookmark>({point, range}));
    QVERIFY(!loaded.entries()[0].missing);
    QVERIFY(!loaded.isModified());
}

void TestProject::missingMediaRemainsInProject()
{
    QTemporaryDir directory;
    const QString projectPath = directory.filePath(QStringLiteral("missing.atkproj"));
    project::Project source;
    source.addSource(std::make_shared<media::MediaSource>(directory.filePath(QStringLiteral("gone.mp4"))));
    QVERIFY(project::ProjectSerializer::save(source, projectPath).ok);
    project::Project loaded;
    QVERIFY(project::ProjectSerializer::load(loaded, projectPath).ok);
    QCOMPARE(loaded.entries().size(), 1);
    QVERIFY(loaded.entries()[0].missing);
}

void TestProject::rejectsMalformedAndUnsupportedFilesWithoutMutation()
{
    QTemporaryDir directory;
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(QStringLiteral("keep.mp4")));
    const int originalCount = project.entries().size();
    QFile malformed(directory.filePath(QStringLiteral("bad.atkproj")));
    QVERIFY(malformed.open(QIODevice::WriteOnly)); malformed.write("{"); malformed.close();
    QVERIFY(!project::ProjectSerializer::load(project, malformed.fileName()).ok);
    QCOMPARE(project.entries().size(), originalCount);
    QFile newer(directory.filePath(QStringLiteral("new.atkproj")));
    QVERIFY(newer.open(QIODevice::WriteOnly));
    newer.write(R"({"format":"ATKProject","version":99,"sources":[]})"); newer.close();
    const auto result = project::ProjectSerializer::load(project, newer.fileName());
    QVERIFY(!result.ok); QVERIFY(result.errorMessage.contains(QStringLiteral("newer")));
    QCOMPARE(project.entries().size(), originalCount);
}

QTEST_MAIN(TestProject)
#include "tst_project.moc"
