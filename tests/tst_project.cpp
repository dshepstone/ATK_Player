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
};

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
