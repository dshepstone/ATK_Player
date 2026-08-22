#include "export/ExportJob.h"
#include "export/ExportRenderer.h"
#include "export/FFmpegExporter.h"
#include "media/MediaDecoder.h"
#include "playback/ComparisonCompositor.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace atk;

namespace {
QString fixture() { return QStringLiteral(ATK_TEST_MEDIA_DIR "/atk_fixture_48f.mkv"); }
QString longFixture() { return QStringLiteral(ATK_TEST_MEDIA_DIR "/atk_sync_10s.mkv"); }

exporter::ExportSpec specFor(const QString& output, qint64 first = 10, qint64 last = 30)
{
    media::MediaDecoder decoder; QString error;
    if (!decoder.open(fixture(), &error)) return {};
    exporter::ExportSpec spec;
    spec.sourceA.path = fixture(); spec.sourceA.metadata = decoder.metadata();
    spec.sourceA.rangeStartFrame = first; spec.sourceA.rangeEndFrame = last;
    spec.outputPath = output; spec.videoEncoder = exporter::FFmpegExporter::availableH264Encoder();
    return spec;
}
}

class TestExport : public QObject {
    Q_OBJECT
private slots:
    void compositorModesAreDeterministic();
    void rendererUsesInclusiveRangeAndRelativePts();
    void exportsDecodableMp4WithAudio();
    void comparisonDifferenceUsesOfflineSources();
    void cancellationLeavesNoOutput();
    void failurePreservesExistingDestination();
};

void TestExport::compositorModesAreDeterministic()
{
    QImage red(3, 3, QImage::Format_RGBA8888); red.fill(Qt::red);
    QImage blue(3, 3, QImage::Format_RGBA8888); blue.fill(Qt::blue);
    const QImage side = playback::ComparisonCompositor::compose(
        red, blue, playback::CompareLayout::SideBySide, 50);
    QCOMPARE(side.size(), QSize(6, 3)); QCOMPARE(side.pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(side.pixelColor(4, 1), QColor(Qt::blue));
    const QImage difference = playback::ComparisonCompositor::compose(
        red, red, playback::CompareLayout::Difference, 50);
    QCOMPARE(difference.pixelColor(1, 1), QColor(Qt::black));
}

void TestExport::rendererUsesInclusiveRangeAndRelativePts()
{
    exporter::ExportSpec spec = specFor(QStringLiteral("unused.mp4"), 10, 12);
    QCOMPARE(spec.frameCount(), qint64(3));
    exporter::ExportRenderer renderer; QString error;
    QVERIFY2(renderer.open(spec, &error), qPrintable(error));
    exporter::RenderedExportFrame first, last;
    QVERIFY2(renderer.render(10, first, &error), qPrintable(error));
    QVERIFY2(renderer.render(12, last, &error), qPrintable(error));
    QCOMPARE(first.outputPtsTicks, qint64(0));
    QVERIFY(last.outputPtsTicks > first.outputPtsTicks);
    QCOMPARE(first.image.size(), spec.outputSize());
}

void TestExport::exportsDecodableMp4WithAudio()
{
    if (!exporter::FFmpegExporter::isSupported()) QSKIP("No permitted H.264 encoder available");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString output = QDir(directory.path()).filePath(QStringLiteral("review.mp4"));
    exporter::ExportJob job(specFor(output));
    QSignalSpy completed(&job, &exporter::ExportJob::completed);
    QSignalSpy failed(&job, &exporter::ExportJob::failed);
    job.start();
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty() || !failed.isEmpty(), 60000);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
    QVERIFY(QFileInfo::exists(output));
    QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.atkpart.*")}, QDir::Files).size(), 0);
    media::MediaDecoder decoder; QString error;
    QVERIFY2(decoder.open(output, &error), qPrintable(error));
    QCOMPARE(decoder.metadata().effectiveFrameCount(), qint64(21));
    QVERIFY(decoder.metadata().hasAudio);
    media::VideoFrame frame; QVERIFY2(decoder.frameAtIndex(0, frame, &error), qPrintable(error));
    QCOMPARE(frame.frameIndex, qint64(0));
}

void TestExport::comparisonDifferenceUsesOfflineSources()
{
    exporter::ExportSpec spec = specFor(QStringLiteral("unused.mp4"), 4, 4);
    spec.comparison = true; spec.layout = playback::CompareLayout::Difference;
    spec.sourceB = spec.sourceA; spec.sourceB.rangeStartFrame = 4; spec.sourceB.rangeEndFrame = 47;
    exporter::ExportRenderer renderer; exporter::RenderedExportFrame frame; QString error;
    QVERIFY2(renderer.open(spec, &error), qPrintable(error));
    QVERIFY2(renderer.render(4, frame, &error), qPrintable(error));
    QCOMPARE(frame.sourceBFrameIndex, qint64(4));
    QCOMPARE(frame.image.pixelColor(frame.image.width() / 2, frame.image.height() / 2), QColor(Qt::black));
}

void TestExport::cancellationLeavesNoOutput()
{
    if (!exporter::FFmpegExporter::isSupported()) QSKIP("No permitted H.264 encoder available");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    media::MediaDecoder decoder; QString error; QVERIFY2(decoder.open(longFixture(), &error), qPrintable(error));
    exporter::ExportSpec spec; spec.sourceA.path = longFixture(); spec.sourceA.metadata = decoder.metadata();
    spec.sourceA.rangeEndFrame = decoder.metadata().effectiveFrameCount() - 1;
    spec.outputPath = QDir(directory.path()).filePath(QStringLiteral("cancelled.mp4"));
    spec.videoEncoder = exporter::FFmpegExporter::availableH264Encoder();
    exporter::ExportJob job(spec); QSignalSpy cancelled(&job, &exporter::ExportJob::cancelled);
    connect(&job, &exporter::ExportJob::progress, &job,
            [&job](int, qint64 frame, qint64) { if (frame >= 2) job.cancel(); });
    job.start(); QTRY_VERIFY_WITH_TIMEOUT(!cancelled.isEmpty(), 30000);
    QVERIFY(!QFileInfo::exists(spec.outputPath));
    QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.atkpart.*")}, QDir::Files).size(), 0);
}

void TestExport::failurePreservesExistingDestination()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString output = QDir(directory.path()).filePath(QStringLiteral("existing.mp4"));
    QFile original(output); QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("original"), qint64(8)); original.close();
    exporter::ExportSpec spec = specFor(output); spec.videoEncoder = QStringLiteral("not_an_encoder");
    exporter::ExportJob job(spec); QSignalSpy failed(&job, &exporter::ExportJob::failed);
    job.start(); QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 30000);
    QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original"));
    QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.atkpart.*")}, QDir::Files).size(), 0);
}

QTEST_GUILESS_MAIN(TestExport)
#include "tst_export.moc"
