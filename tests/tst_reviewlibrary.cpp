#include "review/FolderScanner.h"
#include "review/ReviewLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace atk::review;

namespace {
void touch(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
}

QStringList relativePaths(const QVector<FoundClip>& clips)
{
    QStringList paths;
    for (const FoundClip& clip : clips) paths << clip.relativePath;
    return paths;
}
} // namespace

class TestReviewLibrary : public QObject {
    Q_OBJECT
private slots:
    void missingFileIsAnEmptyLibrary();
    void reviewsRoundTripThroughTheFile();
    void sameProjectReplacesInsteadOfDuplicating();
    void malformedOrNewerFilesAreRefused();
    void naturalOrderPutsTwoBeforeTen();
    void scanFindsClipsInNaturalOrder();
    void scanGroupsImageSequencesAndSkipsHiddenFiles();
    void scanCanIncludeSubfoldersAndBeCancelled();
};

void TestReviewLibrary::missingFileIsAnEmptyLibrary()
{
    QTemporaryDir dir;
    ReviewLibrary library(dir.filePath(QStringLiteral("none/reviews.json")));
    QString error;
    QVERIFY(library.load(&error));
    QVERIFY(library.reviews().isEmpty());
}

void TestReviewLibrary::reviewsRoundTripThroughTheFile()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("app/reviews.json"));
    ReviewLibrary library(file);
    ReviewInfo week;
    week.name = QStringLiteral("Week 13 Capstone");
    week.folder = dir.filePath(QStringLiteral("week13"));
    week.projectPath = dir.filePath(QStringLiteral("week13/Week 13 Capstone.atkproj"));
    week.includeSubfolders = true;
    week.starred = true;
    week.createdMsUtc = 1000;
    week.openedMsUtc = 2000;
    week.clipCount = 20;
    week.previewClips = {dir.filePath(QStringLiteral("week13/a.mp4")), dir.filePath(QStringLiteral("week13/b.mp4"))};
    const QUuid id = library.add(week);
    QVERIFY(!id.isNull());
    ReviewInfo blocking;
    blocking.name = QStringLiteral("blocking demo A2");
    blocking.folder = dir.filePath(QStringLiteral("blocking"));
    blocking.projectPath = dir.filePath(QStringLiteral("blocking/blocking demo A2.atkproj"));
    blocking.archived = true;
    library.add(blocking);
    QString error;
    QVERIFY2(library.save(&error), qPrintable(error));

    ReviewLibrary reread(file);
    QVERIFY(reread.load(&error));
    QCOMPARE(reread.reviews().size(), 2);
    const ReviewInfo* found = reread.find(id);
    QVERIFY(found);
    week.id = id;
    QCOMPARE(*found, week);
    QVERIFY(reread.findByProject(blocking.projectPath));
    QVERIFY(reread.findByProject(blocking.projectPath)->archived);

    // Update and remove.
    ReviewInfo changed = *found;
    changed.starred = false;
    QVERIFY(reread.update(changed));
    QVERIFY(!reread.find(id)->starred);
    QVERIFY(reread.remove(id));
    QVERIFY(!reread.remove(id));
    QCOMPARE(reread.reviews().size(), 1);
}

void TestReviewLibrary::sameProjectReplacesInsteadOfDuplicating()
{
    QTemporaryDir dir;
    ReviewLibrary library(dir.filePath(QStringLiteral("reviews.json")));
    ReviewInfo review;
    review.name = QStringLiteral("First");
    review.projectPath = dir.filePath(QStringLiteral("r/review.atkproj"));
    const QUuid first = library.add(review);
    review.name = QStringLiteral("Second");
    review.projectPath = dir.filePath(QStringLiteral("r/../r/review.atkproj")); // the same file
    QCOMPARE(library.add(review), first);
    QCOMPARE(library.reviews().size(), 1);
    QCOMPARE(library.reviews().first().name, QStringLiteral("Second"));
}

void TestReviewLibrary::malformedOrNewerFilesAreRefused()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("reviews.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
    file.close();
    ReviewLibrary library(path);
    QString error;
    QVERIFY(!library.load(&error));
    QVERIFY(!error.isEmpty());

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"format":"ATKReviewLibrary","version":99,"reviews":[]})");
    file.close();
    QVERIFY(!library.load(&error));
    QVERIFY(error.contains(QStringLiteral("newer")));
}

void TestReviewLibrary::naturalOrderPutsTwoBeforeTen()
{
    QVERIFY(naturalLess(QStringLiteral("clip2.mov"), QStringLiteral("clip10.mov")));
    QVERIFY(!naturalLess(QStringLiteral("clip10.mov"), QStringLiteral("clip2.mov")));
    QVERIFY(naturalLess(QStringLiteral("Adams_J.mp4"), QStringLiteral("baker_s.mp4"))); // case-insensitive
}

void TestReviewLibrary::scanFindsClipsInNaturalOrder()
{
    QTemporaryDir dir;
    for (const char* name : {"clip10.mp4", "clip2.mp4", "Clip1.MOV", "notes.txt", "brief.pdf", "still.jpg"})
        touch(dir.filePath(QString::fromLatin1(name)));
    const QVector<FoundClip> clips = scanFolder(dir.path(), false);
    QCOMPARE(relativePaths(clips), (QStringList{QStringLiteral("Clip1.MOV"), QStringLiteral("clip2.mp4"),
                                                QStringLiteral("clip10.mp4"), QStringLiteral("still.jpg")}));
    QCOMPARE(clips.first().path, QDir(dir.path()).filePath(QStringLiteral("Clip1.MOV")));
    QVERIFY(!clips.first().sequence);
    QVERIFY(scanFolder(dir.filePath(QStringLiteral("missing")), false).isEmpty());
}

void TestReviewLibrary::scanGroupsImageSequencesAndSkipsHiddenFiles()
{
    QTemporaryDir dir;
    for (const char* name : {"shot.0001.png", "shot.0002.png", "shot.0003.png", "ref.0007.png", ".hidden.mp4",
                             ".cache/inside.mp4", "a.mp4"})
        touch(dir.filePath(QString::fromLatin1(name)));
    const QVector<FoundClip> clips = scanFolder(dir.path(), true);
    QCOMPARE(clips.size(), 3); // a.mp4, ref.0007.png (a lone still), the sequence
    const auto sequence = std::find_if(clips.cbegin(), clips.cend(), [](const FoundClip& c) { return c.sequence; });
    QVERIFY(sequence != clips.cend());
    QCOMPARE(sequence->sequenceFirst, qint64(1));
    QCOMPARE(sequence->sequenceLast, qint64(3));
    QVERIFY(sequence->path.contains(QLatin1Char('%')));
    QVERIFY(std::any_of(clips.cbegin(), clips.cend(),
                        [](const FoundClip& c) { return c.relativePath == QStringLiteral("ref.0007.png") && !c.sequence; }));
    for (const FoundClip& clip : clips) QVERIFY(!clip.relativePath.contains(QStringLiteral("hidden")));
    for (const FoundClip& clip : clips) QVERIFY(!clip.relativePath.contains(QStringLiteral("inside")));
}

void TestReviewLibrary::scanCanIncludeSubfoldersAndBeCancelled()
{
    QTemporaryDir dir;
    touch(dir.filePath(QStringLiteral("top.mp4")));
    touch(dir.filePath(QStringLiteral("week2/student.mp4")));
    QCOMPARE(scanFolder(dir.path(), false).size(), 1);
    const QVector<FoundClip> deep = scanFolder(dir.path(), true);
    QCOMPARE(relativePaths(deep), (QStringList{QStringLiteral("top.mp4"), QStringLiteral("week2/student.mp4")}));
    QVERIFY(scanFolder(dir.path(), true, [] { return true; }).isEmpty());
}

QTEST_MAIN(TestReviewLibrary)
#include "tst_reviewlibrary.moc"
