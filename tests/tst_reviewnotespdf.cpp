#include "annotation/AnnotationTypes.h"
#include "export/ReviewNotesPdf.h"
#include "media/MediaDecoder.h"
#include "platform/PlatformInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace atk;

namespace {
QString fixture() { return QDir(QStringLiteral(ATK_TEST_MEDIA_DIR)).filePath(QStringLiteral("atk_export_120f.mkv")); }

exporter::ExportSpec specForFixture()
{
    media::MediaDecoder decoder;
    QString error;
    if (!decoder.open(fixture(), &error)) return {};
    exporter::ExportSpec spec;
    spec.sourceA.path = fixture();
    spec.sourceA.metadata = decoder.metadata();
    spec.sourceA.rangeStartFrame = 0;
    spec.sourceA.rangeEndFrame = 119;
    return spec;
}

annotation::AnnotationItem stroke(quint64 id)
{
    annotation::AnnotationItem item;
    item.id = id;
    item.tool = annotation::AnnotationTool::Marker;
    item.color = Qt::red;
    item.size = 0.05;
    item.points = {{0.2f, 0.2f, 1, 0, 0}, {0.6f, 0.5f, 1, 0, 0}};
    return item;
}

annotation::FrameNote note(quint64 id, const QString& text, bool resolved = false)
{
    return {id, text, QStringLiteral("David"), 0, resolved};
}

/// Number of pages in a PDF, read from its page tree without a PDF library.
int pdfPageCount(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    const QByteArray data = file.readAll();
    if (!data.startsWith("%PDF")) return -1;
    int count = 0;
    for (qsizetype at = data.indexOf("/Type /Page"); at >= 0; at = data.indexOf("/Type /Page", at + 1))
        if (data.mid(at + 11, 1) != "s") ++count;
    return count;
}
} // namespace

class TestReviewNotesPdf : public QObject {
    Q_OBJECT
private slots:
    void paginateKeepsRowsWhole();
    void framesToIncludeFollowsDrawingsAndNotes();
    void writerProducesAPagePerOverflow();
    void jobRendersAnnotatedFramesIntoAPdf();
    void jobCancelLeavesNoFile();
    void verifyCatchesBrokenFiles();
    void pagePicturesFallbackIsAValidPdfWithoutFonts();
    void jobSavesBesideAFileItCannotReplace();
    void jobSavesBesideAFileOpenInAViewer();
};

void TestReviewNotesPdf::paginateKeepsRowsWhole()
{
    using W = exporter::ReviewNotesPdfWriter;
    QCOMPARE(W::paginate({}, 100, 200), QVector<int>{});
    // Gap is 12pt: 40+12+40 fits in 100 (second row starts at 52, needs 40).
    QCOMPARE(W::paginate({40, 40, 40}, 100, 200), (QVector<int>{0, 0, 1}));
    // A row taller than a page still gets a page of its own.
    QCOMPARE(W::paginate({300, 10}, 100, 200), (QVector<int>{0, 1}));
    QCOMPARE(W::paginate({10, 300, 10}, 100, 200), (QVector<int>{0, 1, 2}));
}

void TestReviewNotesPdf::framesToIncludeFollowsDrawingsAndNotes()
{
    annotation::SourceAnnotations set;
    annotation::FrameAnnotation drawn;
    drawn.frame = 5;
    drawn.items = {stroke(1)};
    set.setFrame(drawn);
    annotation::FrameAnnotation open;
    open.frame = 2;
    open.notes = {note(2, QStringLiteral("open"))};
    set.setFrame(open);
    annotation::FrameAnnotation done;
    done.frame = 9;
    done.notes = {note(3, QStringLiteral("done"), true)};
    set.setFrame(done);

    QCOMPARE(exporter::ReviewNotesJob::framesToInclude(set, true), (QVector<qint64>{2, 5, 9}));
    QCOMPARE(exporter::ReviewNotesJob::framesToInclude(set, false), (QVector<qint64>{2, 5}));
}

void TestReviewNotesPdf::writerProducesAPagePerOverflow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    exporter::ReviewNotesDocument doc;
    doc.projectName = QStringLiteral("Project");
    doc.clipName = QStringLiteral("clip.mov");
    QImage picture(320, 180, QImage::Format_RGB32);
    picture.fill(Qt::darkGray);
    for (int i = 0; i < 12; ++i)
        doc.rows.append({i, QStringLiteral("00:00:00:%1").arg(i, 2, 10, QLatin1Char('0')), picture,
                         {note(quint64(i + 1), QStringLiteral("Note %1").arg(i))}});

    const QString one = dir.filePath(QStringLiteral("one.pdf"));
    doc.rows.resize(1);
    QString error;
    QVERIFY2(exporter::ReviewNotesPdfWriter::write(doc, one, &error), qPrintable(error));
    QCOMPARE(pdfPageCount(one), 1);

    // Twelve 16:9 rows (about 140pt each on A4) cannot fit on one page.
    for (int i = 1; i < 12; ++i)
        doc.rows.append({i, QString(), picture, {note(quint64(i + 1), QStringLiteral("Note %1").arg(i))}});
    const QString many = dir.filePath(QStringLiteral("many.pdf"));
    QVERIFY2(exporter::ReviewNotesPdfWriter::write(doc, many, &error), qPrintable(error));
    QVERIFY(pdfPageCount(many) >= 3);
}

void TestReviewNotesPdf::jobRendersAnnotatedFramesIntoAPdf()
{
    exporter::ExportSpec spec = specForFixture();
    QVERIFY(!spec.sourceA.path.isEmpty());
    annotation::FrameAnnotation a;
    a.frame = 10;
    a.items = {stroke(1)};
    a.notes = {note(2, QStringLiteral("Push the pose further"))};
    spec.annotations.setFrame(a);
    annotation::FrameAnnotation b;
    b.frame = 40;
    b.notes = {note(3, QStringLiteral("Resolved already"), true)};
    spec.annotations.setFrame(b);

    QTemporaryDir dir;
    exporter::ReviewNotesOptions options;
    options.outputPath = dir.filePath(QStringLiteral("notes.pdf"));
    options.imageWidth = 320;
    exporter::ReviewNotesDocument header;
    header.projectName = QStringLiteral("Test project");
    header.clipName = QStringLiteral("atk_export_120f.mkv");
    exporter::ReviewNotesJob job(spec, header, options);
    QSignalSpy completed(&job, &exporter::ReviewNotesJob::completed);
    QSignalSpy failed(&job, &exporter::ReviewNotesJob::failed);
    job.start();
    QVERIFY(completed.wait(30000) || !failed.isEmpty());
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
    QCOMPARE(completed.first().at(0).toString(), options.outputPath);
    const int pages = completed.first().at(1).toInt();
    QCOMPARE(pages, 1);
    QCOMPARE(pdfPageCount(options.outputPath), pages);
    QVERIFY(!QFile::exists(options.outputPath + QStringLiteral(".partial")));
}

void TestReviewNotesPdf::jobCancelLeavesNoFile()
{
    exporter::ExportSpec spec = specForFixture();
    for (qint64 f = 0; f < 60; ++f) {
        annotation::FrameAnnotation frame;
        frame.frame = f;
        frame.items = {stroke(quint64(f + 1))};
        spec.annotations.setFrame(frame);
    }
    QTemporaryDir dir;
    exporter::ReviewNotesOptions options;
    options.outputPath = dir.filePath(QStringLiteral("cancelled.pdf"));
    exporter::ReviewNotesJob job(spec, {}, options);
    QSignalSpy cancelled(&job, &exporter::ReviewNotesJob::cancelled);
    job.start();
    job.cancel();
    job.wait();
    QCoreApplication::processEvents();
    QVERIFY(!QFile::exists(options.outputPath));
    QVERIFY(!QFile::exists(options.outputPath + QStringLiteral(".partial")));
}

void TestReviewNotesPdf::verifyCatchesBrokenFiles()
{
    QTemporaryDir dir;
    exporter::ReviewNotesDocument doc;
    doc.projectName = QStringLiteral("Project");
    QImage picture(320, 180, QImage::Format_RGB32);
    picture.fill(Qt::darkGray);
    doc.rows.append({0, QStringLiteral("00:00:00:00"), picture, {note(1, QStringLiteral("Résumé ✓ 日本"), true)}});
    const QString good = dir.filePath(QStringLiteral("good.pdf"));
    QString error;
    QVERIFY(exporter::ReviewNotesPdfWriter::write(doc, good, &error));
    QVERIFY2(exporter::ReviewNotesPdfWriter::verify(good, &error), qPrintable(error));

    QFile file(good);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();
    auto broken = [&](const QByteArray& data) {
        const QString path = dir.filePath(QStringLiteral("broken.pdf"));
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        out.write(data);
        out.close();
        QString why;
        return !exporter::ReviewNotesPdfWriter::verify(path, &why) && !why.isEmpty();
    };
    QVERIFY(broken(bytes.left(bytes.size() / 2)));          // truncated
    QVERIFY(broken(QByteArray()));                         // empty
    QVERIFY(broken(QByteArray("hello") + bytes.mid(5)));   // no header
    QByteArray shifted = bytes;
    shifted.insert(bytes.size() / 3, "garbage ");          // objects moved
    QVERIFY(broken(shifted));
    QVERIFY(!exporter::ReviewNotesPdfWriter::verify(dir.filePath(QStringLiteral("missing.pdf")), &error));
}

void TestReviewNotesPdf::pagePicturesFallbackIsAValidPdfWithoutFonts()
{
    QTemporaryDir dir;
    exporter::ReviewNotesDocument doc;
    doc.projectName = QStringLiteral("Project");
    doc.clipName = QStringLiteral("clip.mov");
    QImage picture(320, 180, QImage::Format_RGB32);
    picture.fill(Qt::darkGray);
    for (int i = 0; i < 12; ++i)
        doc.rows.append({i, QStringLiteral("00:00:00:00"), picture, {note(quint64(i + 1), QStringLiteral("Note %1").arg(i))}});
    QString error;
    int normalPages = 0;
    bool fallback = true;
    const QString normal = dir.filePath(QStringLiteral("normal.pdf"));
    QVERIFY2(exporter::ReviewNotesPdfWriter::write(doc, normal, &error, &normalPages, &fallback), qPrintable(error));
    QVERIFY(!fallback);
    int picturePages = 0;
    const QString pictures = dir.filePath(QStringLiteral("pictures.pdf"));
    QVERIFY2(exporter::ReviewNotesPdfWriter::write(doc, pictures, &error, &picturePages, &fallback,
                                                   exporter::ReviewNotesPdfWriter::Mode::PagePictures),
             qPrintable(error));
    QVERIFY(fallback);
    QCOMPARE(picturePages, normalPages); // same layout, page for page
    QCOMPARE(pdfPageCount(pictures), picturePages);
    QVERIFY(exporter::ReviewNotesPdfWriter::verify(pictures, &error));
    {
        QFile file(pictures);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("/FontFile")); // nothing font-related to go wrong
        QFile normalFile(normal);
        QVERIFY(normalFile.open(QIODevice::ReadOnly));
        QVERIFY(normalFile.readAll().contains("/FontFile")); // the normal PDF has real, searchable text
    } // closed: Windows cannot replace a file that is still open

    // Saving replaces an existing file in one step.
    QVERIFY(exporter::ReviewNotesPdfWriter::write(doc, pictures, &error));
    QVERIFY(exporter::ReviewNotesPdfWriter::verify(pictures, &error));
}

// When the chosen PDF cannot be replaced (on Windows: it is open in a viewer,
// often because the last export opened it), the job saves "name (2).pdf".
void TestReviewNotesPdf::jobSavesBesideAFileItCannotReplace()
{
    exporter::ExportSpec spec = specForFixture();
    annotation::FrameAnnotation a;
    a.frame = 10;
    a.items = {stroke(1)};
    spec.annotations.setFrame(a);
    QTemporaryDir dir;
    exporter::ReviewNotesOptions options;
    options.outputPath = dir.filePath(QStringLiteral("notes.pdf"));
    options.imageWidth = 320;
    // A directory in the way can never be replaced by a file: stands in for a
    // locked file portably.
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("notes.pdf/inside")));
    exporter::ReviewNotesJob job(spec, {}, options);
    QSignalSpy completed(&job, &exporter::ReviewNotesJob::completed);
    QSignalSpy failed(&job, &exporter::ReviewNotesJob::failed);
    job.start();
    QVERIFY(completed.wait(30000) || !failed.isEmpty());
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
    const QString saved = completed.first().at(0).toString();
    QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("notes (2).pdf"));
    QString error;
    QVERIFY(exporter::ReviewNotesPdfWriter::verify(saved, &error));
    QCOMPARE(exporter::numberedSibling(options.outputPath), dir.filePath(QStringLiteral("notes (3).pdf")));
}

// A viewer holding the last PDF open: on Windows the new PDF must not replace
// it (the viewer would show "access denied"); elsewhere replacing is safe.
void TestReviewNotesPdf::jobSavesBesideAFileOpenInAViewer()
{
    exporter::ExportSpec spec = specForFixture();
    annotation::FrameAnnotation a;
    a.frame = 10;
    a.items = {stroke(1)};
    spec.annotations.setFrame(a);
    QTemporaryDir dir;
    exporter::ReviewNotesOptions options;
    options.outputPath = dir.filePath(QStringLiteral("open.pdf"));
    options.imageWidth = 320;
    {
        QFile previous(options.outputPath);
        QVERIFY(previous.open(QIODevice::WriteOnly));
        previous.write("%PDF-old");
    }
    QFile viewer(options.outputPath);
    QVERIFY(viewer.open(QIODevice::ReadOnly)); // the "viewer"
    QCOMPARE(platform::PlatformInfo::isFileOpenElsewhere(options.outputPath),
#ifdef Q_OS_WIN
             true
#else
             false
#endif
    );
    exporter::ReviewNotesJob job(spec, {}, options);
    QSignalSpy completed(&job, &exporter::ReviewNotesJob::completed);
    QSignalSpy failed(&job, &exporter::ReviewNotesJob::failed);
    job.start();
    QVERIFY(completed.wait(30000) || !failed.isEmpty());
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
    const QString saved = completed.first().at(0).toString();
#ifdef Q_OS_WIN
    QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("open (2).pdf"));
    QCOMPARE(viewer.readAll(), QByteArray("%PDF-old")); // the open file was left alone
#else
    QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("open.pdf"));
#endif
    viewer.close();
    QString error;
    QVERIFY(exporter::ReviewNotesPdfWriter::verify(saved, &error));
    QVERIFY(!platform::PlatformInfo::isFileOpenElsewhere(saved));
    QVERIFY(!platform::PlatformInfo::isFileOpenElsewhere(dir.filePath(QStringLiteral("missing.pdf"))));
}

QTEST_MAIN(TestReviewNotesPdf)
#include "tst_reviewnotespdf.moc"
