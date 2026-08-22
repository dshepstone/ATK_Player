#include "export/ExportJob.h"
#include "export/ExportRenderer.h"
#include "export/FFmpegExporter.h"
#include "media/MediaDecoder.h"
#include "playback/ComparisonCompositor.h"
#include "project/Project.h"
#include "project/ProjectSerializer.h"
#include "ui/MainWindow.h"
#include "api/ApiServer.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
}
using namespace atk;

namespace {
QString mediaFile(const char* name) { return QDir(QStringLiteral(ATK_TEST_MEDIA_DIR)).filePath(QString::fromLatin1(name)); }
QString fixture() { return mediaFile("atk_fixture_48f.mkv"); }
QString exportFixture() { return mediaFile("atk_export_120f.mkv"); }
QString fractionalFixture() { return mediaFile("atk_export_23976_120f.mkv"); }
QString longFixture() { return mediaFile("atk_sync_10s.mkv"); }

exporter::ExportSpec specFor(const QString& source, const QString& output, qint64 first, qint64 last)
{
    media::MediaDecoder decoder; QString error;
    if (!decoder.open(source, &error)) return {};
    exporter::ExportSpec spec;
    spec.sourceA.path = source; spec.sourceA.metadata = decoder.metadata();
    spec.sourceA.rangeStartFrame = first; spec.sourceA.rangeEndFrame = last;
    spec.outputPath = output; spec.videoEncoder = exporter::FFmpegExporter::availableH264Encoder();
    return spec;
}

bool runExport(const exporter::ExportSpec& spec, QString* failure = nullptr)
{
    exporter::ExportJob job(spec); QSignalSpy completed(&job, &exporter::ExportJob::completed);
    QSignalSpy failed(&job, &exporter::ExportJob::failed); job.start();
    QElapsedTimer timer; timer.start();
    while (completed.isEmpty() && failed.isEmpty() && timer.elapsed() < 60000) completed.wait(100);
    if (completed.isEmpty() && failed.isEmpty()) { if (failure) *failure = QStringLiteral("Export timed out"); return false; }
    if (!failed.isEmpty()) { if (failure) *failure = failed.first().first().toString(); return false; }
    return !completed.isEmpty();
}

struct OutputStats {
    AVRational timeBase{}, rFrameRate{}, avgFrameRate{};
    qint64 duration = 0, metadataFrames = 0, packets = 0, decodedFrames = 0;
    AVRational audioTimeBase{};
    qint64 audioDuration = -1;
    std::vector<qint64> pts;
    QImage firstImage, lastImage;
};

OutputStats inspectOutput(const QString& path, QString* error)
{
    OutputStats out; AVFormatContext* format = nullptr;
    const QByteArray encoded = QFile::encodeName(path);
    if (avformat_open_input(&format, encoded.constData(), nullptr, nullptr) < 0
        || avformat_find_stream_info(format, nullptr) < 0) {
        if (error) *error = QStringLiteral("Unable to inspect exported MP4");
        if (format) avformat_close_input(&format); return out;
    }
    const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex >= 0) {
        const AVStream* stream = format->streams[streamIndex];
        out.timeBase = stream->time_base; out.rFrameRate = stream->r_frame_rate;
        out.avgFrameRate = stream->avg_frame_rate; out.duration = stream->duration;
        out.metadataFrames = stream->nb_frames;
        AVPacket* packet = av_packet_alloc();
        while (av_read_frame(format, packet) >= 0) {
            if (packet->stream_index == streamIndex) ++out.packets;
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
    }
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const AVStream* stream = format->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            out.audioTimeBase = stream->time_base; out.audioDuration = stream->duration; break;
        }
    }
    avformat_close_input(&format);
    media::MediaDecoder decoder;
    if (!decoder.open(path, error)) return {};
    media::VideoFrame frame;
    while (decoder.nextVideoFrame(frame, error) == media::DecodeStatus::Ok) {
        if (out.decodedFrames == 0) out.firstImage = frame.image;
        out.lastImage = frame.image; out.pts.push_back(frame.ptsTicks); ++out.decodedFrames;
    }
    return out;
}

void verifyTiming(const OutputStats& stats, qint64 count, int rateNum, int rateDen)
{
    QCOMPARE(stats.metadataFrames, count); QCOMPARE(stats.packets, count); QCOMPARE(stats.decodedFrames, count);
    QCOMPARE(stats.rFrameRate.num, rateNum); QCOMPARE(stats.rFrameRate.den, rateDen);
    QCOMPARE(stats.avgFrameRate.num, rateNum); QCOMPARE(stats.avgFrameRate.den, rateDen);
    QCOMPARE(stats.pts.size(), static_cast<size_t>(count));
    for (qint64 i = 1; i < count; ++i)
        QCOMPARE((stats.pts[i] - stats.pts[i - 1]) * stats.timeBase.num * rateNum,
                 static_cast<qint64>(stats.timeBase.den) * rateDen);
    QCOMPARE(stats.duration * stats.timeBase.num * rateNum,
             count * static_cast<qint64>(stats.timeBase.den) * rateDen);
    if (stats.audioDuration >= 0)
        QCOMPARE(stats.audioDuration * stats.audioTimeBase.num * rateNum,
                 count * static_cast<qint64>(stats.audioTimeBase.den) * rateDen);
}

double imageError(const QImage& one, const QImage& two)
{
    const QImage a = one.scaled(32, 18).convertToFormat(QImage::Format_RGB32);
    const QImage b = two.scaled(32, 18).convertToFormat(QImage::Format_RGB32);
    qint64 difference = 0;
    for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
        const QColor ac = a.pixelColor(x, y), bc = b.pixelColor(x, y);
        difference += qAbs(ac.red() - bc.red()) + qAbs(ac.green() - bc.green()) + qAbs(ac.blue() - bc.blue());
    }
    return difference / static_cast<double>(a.width() * a.height() * 3);
}
}

class TestExport : public QObject {
    Q_OBJECT
private slots:
    void compositorModesAreDeterministic();
    void rendererUsesInclusiveRangeAndRelativePts();
    void exactCfrRanges_data(); void exactCfrRanges();
    void humanRangePreservesFirstAndLastImages();
    void fullLegacyClipHas48Frames();
    void fractionalRatePreservesExactCadence();
    void comparisonModesKeepSourceAFrameCount_data(); void comparisonModesKeepSourceAFrameCount();
    void offsetsDoNotChangeSourceAFrameCount();
    void cancellationLeavesNoOutput();
    void failurePreservesExistingDestination();
    void singleFramePngUsesExactFrameAndContentDimensions();
    void comparisonStillUsesExistingCompositorMapping();
    void imageSequenceUsesInclusiveVisibleFrameNames();
    void imageExportBoundariesCollisionFailureAndCancellation();
    void apiImageExportsValidateAndShareStatus();
    void manualPrivateMediaAcceptance();
};

void TestExport::compositorModesAreDeterministic()
{
    QImage red(3, 3, QImage::Format_RGBA8888); red.fill(Qt::red);
    QImage blue(3, 3, QImage::Format_RGBA8888); blue.fill(Qt::blue);
    const QImage side = playback::ComparisonCompositor::compose(red, blue, playback::CompareLayout::SideBySide, 50);
    QCOMPARE(side.size(), QSize(6, 3)); QCOMPARE(side.pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(side.pixelColor(4, 1), QColor(Qt::blue));
    const QImage difference = playback::ComparisonCompositor::compose(red, red, playback::CompareLayout::Difference, 50);
    QCOMPARE(difference.pixelColor(1, 1), QColor(Qt::black));
}

void TestExport::rendererUsesInclusiveRangeAndRelativePts()
{
    exporter::ExportSpec spec = specFor(fixture(), QStringLiteral("unused.mp4"), 10, 12);
    QCOMPARE(spec.frameCount(), qint64(3));
    exporter::ExportRenderer renderer; QString error; QVERIFY2(renderer.open(spec, &error), qPrintable(error));
    exporter::RenderedExportFrame first, last;
    QVERIFY2(renderer.render(10, first, &error), qPrintable(error));
    QVERIFY2(renderer.render(12, last, &error), qPrintable(error));
    QCOMPARE(first.outputPtsTicks, qint64(0)); QVERIFY(last.outputPtsTicks > first.outputPtsTicks);
}

void TestExport::exactCfrRanges_data()
{
    QTest::addColumn<qint64>("first"); QTest::addColumn<qint64>("last"); QTest::addColumn<qint64>("count");
    QTest::newRow("one-frame") << qint64(10) << qint64(10) << qint64(1);
    QTest::newRow("two-frames") << qint64(10) << qint64(11) << qint64(2);
    QTest::newRow("three-frames") << qint64(10) << qint64(12) << qint64(3);
    QTest::newRow("human-30") << qint64(45) << qint64(74) << qint64(30);
    QTest::newRow("human-31") << qint64(45) << qint64(75) << qint64(31);
    QTest::newRow("full-clip") << qint64(0) << qint64(119) << qint64(120);
}

void TestExport::exactCfrRanges()
{
    if (!exporter::FFmpegExporter::isSupported()) QSKIP("No permitted H.264 encoder available");
    QFETCH(qint64, first); QFETCH(qint64, last); QFETCH(qint64, count);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString output = QDir(directory.path()).filePath(QStringLiteral("review.mp4"));
    const auto spec = specFor(exportFixture(), output, first, last);
    QCOMPARE(spec.frameCount(), count); QString error; QVERIFY2(runExport(spec, &error), qPrintable(error));
    const OutputStats stats = inspectOutput(output, &error); QVERIFY2(error.isEmpty(), qPrintable(error));
    verifyTiming(stats, count, 24, 1);
}

void TestExport::humanRangePreservesFirstAndLastImages()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
    const QString output = QDir(directory.path()).filePath(QStringLiteral("human.mp4"));
    const auto spec = specFor(exportFixture(), output, 45, 75); QVERIFY2(runExport(spec, &error), qPrintable(error));
    const OutputStats stats = inspectOutput(output, &error); QVERIFY2(error.isEmpty(), qPrintable(error));
    media::MediaDecoder source; QVERIFY2(source.open(exportFixture(), &error), qPrintable(error));
    media::VideoFrame first, last; QVERIFY(source.frameAtIndex(45, first, &error)); QVERIFY(source.frameAtIndex(75, last, &error));
    QVERIFY(imageError(stats.firstImage, first.image) < 12.0); QVERIFY(imageError(stats.lastImage, last.image) < 12.0);
}

void TestExport::fullLegacyClipHas48Frames()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
    const QString output = QDir(directory.path()).filePath(QStringLiteral("full48.mp4"));
    const auto spec = specFor(fixture(), output, 0, 47); QCOMPARE(spec.frameCount(), qint64(48));
    QVERIFY2(runExport(spec, &error), qPrintable(error));
    const OutputStats stats = inspectOutput(output, &error); QVERIFY2(error.isEmpty(), qPrintable(error));
    verifyTiming(stats, 48, 24, 1);
}

void TestExport::fractionalRatePreservesExactCadence()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
    const QString output = QDir(directory.path()).filePath(QStringLiteral("fractional.mp4"));
    const auto spec = specFor(fractionalFixture(), output, 45, 75); QCOMPARE(spec.frameCount(), qint64(31));
    QVERIFY2(runExport(spec, &error), qPrintable(error));
    const OutputStats stats = inspectOutput(output, &error); QVERIFY2(error.isEmpty(), qPrintable(error));
    verifyTiming(stats, 31, 24000, 1001);
}

void TestExport::comparisonModesKeepSourceAFrameCount_data()
{
    QTest::addColumn<int>("layout");
    for (int value = 0; value <= 4; ++value) QTest::newRow(qPrintable(QString::number(value))) << value;
}

void TestExport::comparisonModesKeepSourceAFrameCount()
{
    QFETCH(int, layout); QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
    auto spec = specFor(exportFixture(), QDir(directory.path()).filePath(QStringLiteral("compare.mp4")), 45, 47);
    spec.comparison = true; spec.layout = static_cast<playback::CompareLayout>(layout);
    spec.sourceB = spec.sourceA; spec.sourceB.rangeStartFrame = 45; spec.sourceB.rangeEndFrame = 119;
    QVERIFY2(runExport(spec, &error), qPrintable(error));
    const OutputStats stats = inspectOutput(spec.outputPath, &error); verifyTiming(stats, 3, 24, 1);
    if (spec.layout == playback::CompareLayout::Difference)
        QVERIFY(stats.firstImage.pixelColor(stats.firstImage.width() / 2, stats.firstImage.height() / 2).value() < 8);
}

void TestExport::offsetsDoNotChangeSourceAFrameCount()
{
    auto spec = specFor(exportFixture(), QStringLiteral("unused.mp4"), 45, 75);
    spec.comparison = true; spec.sourceB = spec.sourceA; spec.sourceB.rangeEndFrame = 119;
    for (qint64 offset : {-41'667LL, 0LL, 41'667LL}) { spec.sourceBOffsetUs = offset; QCOMPARE(spec.frameCount(), qint64(31)); }
    spec.audioMode = playback::CompareAudioMode::External; spec.externalAudioOffsetUs = 2'000'000;
    QCOMPARE(spec.frameCount(), qint64(31));
}

void TestExport::cancellationLeavesNoOutput()
{
    if (!exporter::FFmpegExporter::isSupported()) QSKIP("No permitted H.264 encoder available");
    QTemporaryDir directory; QVERIFY(directory.isValid());
    auto spec = specFor(longFixture(), QDir(directory.path()).filePath(QStringLiteral("cancelled.mp4")), 0, 239);
    exporter::ExportJob job(spec); QSignalSpy cancelled(&job, &exporter::ExportJob::cancelled);
    connect(&job, &exporter::ExportJob::progress, &job, [&job](int, qint64 frame, qint64) { if (frame >= 2) job.cancel(); });
    job.start(); QTRY_VERIFY_WITH_TIMEOUT(!cancelled.isEmpty(), 30000);
    QVERIFY(!QFileInfo::exists(spec.outputPath));
    QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.atkpart.*")}, QDir::Files).size(), 0);
}

void TestExport::failurePreservesExistingDestination()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString output = QDir(directory.path()).filePath(QStringLiteral("existing.mp4"));
    QFile original(output); QVERIFY(original.open(QIODevice::WriteOnly)); QCOMPARE(original.write("original"), qint64(8)); original.close();
    auto spec = specFor(fixture(), output, 10, 30); spec.videoEncoder = QStringLiteral("not_an_encoder");
    exporter::ExportJob job(spec); QSignalSpy failed(&job, &exporter::ExportJob::failed);
    job.start(); QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 30000);
    QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original"));
}

void TestExport::singleFramePngUsesExactFrameAndContentDimensions()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString output = QDir(directory.path()).filePath(QStringLiteral("frame_0051.png"));
    auto spec = specFor(exportFixture(), output, 0, 119);
    spec.kind = exporter::ExportKind::CurrentFrame;
    spec.firstFrame = spec.lastFrame = 50;
    QString error; QVERIFY2(runExport(spec, &error), qPrintable(error));
    QImage png(output); QVERIFY(!png.isNull());
    QCOMPARE(png.size(), spec.contentSize());
    media::MediaDecoder decoder; QVERIFY(decoder.open(exportFixture(), &error));
    media::VideoFrame expected; QVERIFY(decoder.frameAtIndex(50, expected, &error));
    QCOMPARE(png, expected.image);
}

void TestExport::comparisonStillUsesExistingCompositorMapping()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    auto spec = specFor(exportFixture(), QDir(directory.path()).filePath(QStringLiteral("difference.png")), 0, 119);
    spec.kind = exporter::ExportKind::CurrentFrame;
    spec.firstFrame = spec.lastFrame = 50;
    spec.comparison = true; spec.sourceB = spec.sourceA;
    spec.layout = playback::CompareLayout::Difference;
    QString error; QVERIFY2(runExport(spec, &error), qPrintable(error));
    QImage png(spec.outputPath); QVERIFY(!png.isNull());
    QCOMPARE(png.size(), spec.contentSize());
    QCOMPARE(png.pixelColor(png.width() / 2, png.height() / 2), QColor(Qt::black));
}

void TestExport::imageSequenceUsesInclusiveVisibleFrameNames()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString output = QDir(directory.path()).filePath(QStringLiteral("sequence"));
    auto spec = specFor(exportFixture(), output, 20, 40);
    spec.kind = exporter::ExportKind::ImageSequence;
    spec.imagePrefix = QStringLiteral("shot");
    QString error; QVERIFY2(runExport(spec, &error), qPrintable(error));
    const QStringList files = QDir(output).entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    QCOMPARE(files.size(), 21);
    QCOMPARE(files.first(), QStringLiteral("shot_0021.png"));
    QCOMPARE(files.last(), QStringLiteral("shot_0041.png"));
    for (int visible = 21; visible <= 41; ++visible)
        QVERIFY(QFileInfo::exists(QDir(output).filePath(
            QStringLiteral("shot_%1.png").arg(visible, 4, 10, QLatin1Char('0')))));
    QImage first(QDir(output).filePath(files.first()));
    QImage last(QDir(output).filePath(files.last()));
    QVERIFY(!first.isNull() && !last.isNull());
    QCOMPARE(first.size(), spec.contentSize()); QCOMPARE(last.size(), spec.contentSize());
}

void TestExport::imageExportBoundariesCollisionFailureAndCancellation()
{
    QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
    for (qint64 frame : {qint64(0), qint64(119)}) {
        auto spec = specFor(exportFixture(), QDir(directory.path()).filePath(
            QStringLiteral("boundary_%1.png").arg(frame)), 0, 119);
        spec.kind = exporter::ExportKind::CurrentFrame;
        spec.firstFrame = spec.lastFrame = frame;
        QVERIFY2(runExport(spec, &error), qPrintable(error));
        QVERIFY(!QImage(spec.outputPath).isNull());
    }

    const QString collision = QDir(directory.path()).filePath(QStringLiteral("existing_sequence"));
    QVERIFY(QDir().mkpath(collision));
    QFile marker(QDir(collision).filePath(QStringLiteral("keep.txt")));
    QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("keep"); marker.close();
    auto collisionSpec = specFor(exportFixture(), collision, 0, 2);
    collisionSpec.kind = exporter::ExportKind::ImageSequence;
    collisionSpec.imagePrefix = QStringLiteral("shot");
    QVERIFY(!runExport(collisionSpec, &error));
    QVERIFY(QFileInfo::exists(marker.fileName()));
    QCOMPARE(QDir(collision).entryList(QDir::Files).size(), 1);

    const QString preserved = QDir(directory.path()).filePath(QStringLiteral("preserved.png"));
    QFile original(preserved); QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("original"); original.close();
    auto failed = specFor(exportFixture(), preserved, 0, 119);
    failed.kind = exporter::ExportKind::CurrentFrame;
    failed.firstFrame = failed.lastFrame = 9999;
    QVERIFY(!runExport(failed, &error));
    QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original"));

    const QString cancelledPath = QDir(directory.path()).filePath(QStringLiteral("cancelled_sequence"));
    auto cancelledSpec = specFor(longFixture(), cancelledPath, 0, 239);
    cancelledSpec.kind = exporter::ExportKind::ImageSequence;
    cancelledSpec.imagePrefix = QStringLiteral("cancel");
    exporter::ExportJob job(cancelledSpec);
    QSignalSpy cancelled(&job, &exporter::ExportJob::cancelled);
    connect(&job, &exporter::ExportJob::progress, &job,
            [&job](int, qint64 frame, qint64) { if (frame >= 2) job.cancel(); });
    job.start(); QTRY_VERIFY_WITH_TIMEOUT(!cancelled.isEmpty(), 20000); job.wait();
    QVERIFY(!QFileInfo::exists(cancelledPath));
    QVERIFY(QDir(directory.path()).entryList({QStringLiteral("cancelled_sequence.atkpart.*")},
                                             QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

void TestExport::apiImageExportsValidateAndShareStatus()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    project::Project project;
    project.addSource(std::make_shared<media::MediaSource>(exportFixture()));
    project.setActiveIndex(0);
    const QString projectPath = QDir(directory.path()).filePath(QStringLiteral("api.atkproj"));
    QVERIFY(project::ProjectSerializer::save(project, projectPath).ok);
    ui::MainWindow window(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));
    QVERIFY(window.openProjectFile(projectPath));
    QTRY_COMPARE_WITH_TIMEOUT(window.playbackController()->state(), playback::PlayerState::Ready, 10000);
    auto request = [&](const QString& command, const QJsonObject& params = {}) {
        return window.apiServer()->handleRequest(QJsonObject{
            {QStringLiteral("command"), command}, {QStringLiteral("params"), params}});
    };
    QVERIFY(!request(QStringLiteral("export_frame"),
                     {{QStringLiteral("path"), QStringLiteral("relative.png")}}).ok);
    QVERIFY(!request(QStringLiteral("export_image_sequence"),
                     {{QStringLiteral("directory"), QStringLiteral("relative")}}).ok);

    const QString framePath = QDir(directory.path()).filePath(QStringLiteral("api_0051.png"));
    const auto frame = request(QStringLiteral("export_frame"), {
        {QStringLiteral("path"), framePath}, {QStringLiteral("frame"), 50}});
    QVERIFY(frame.ok); QVERIFY(!frame.result.value(QStringLiteral("jobId")).toString().isEmpty());
    QJsonObject status;
    QTRY_VERIFY_WITH_TIMEOUT((status = request(QStringLiteral("get_export_status")).result)
                                 .value(QStringLiteral("state")).toString() == QStringLiteral("completed"), 20000);
    QCOMPARE(status.value(QStringLiteral("totalFrames")).toInt(), 1);
    QVERIFY(!QImage(framePath).isNull());

    const QString sequencePath = QDir(directory.path()).filePath(QStringLiteral("api_sequence"));
    const auto sequence = request(QStringLiteral("export_image_sequence"), {
        {QStringLiteral("directory"), sequencePath}, {QStringLiteral("prefix"), QStringLiteral("api")},
        {QStringLiteral("startFrame"), 20}, {QStringLiteral("endFrame"), 40}});
    QVERIFY(sequence.ok);
    QTRY_VERIFY_WITH_TIMEOUT(request(QStringLiteral("get_export_status")).result
                                 .value(QStringLiteral("state")).toString() == QStringLiteral("completed"), 20000);
    QCOMPARE(QDir(sequencePath).entryList({QStringLiteral("*.png")}, QDir::Files).size(), 21);
    QVERIFY(QFileInfo::exists(QDir(sequencePath).filePath(QStringLiteral("api_0021.png"))));
    QVERIFY(QFileInfo::exists(QDir(sequencePath).filePath(QStringLiteral("api_0041.png"))));
    QVERIFY(!request(QStringLiteral("export_image_sequence"),
                     {{QStringLiteral("directory"), sequencePath}}).ok);
}

void TestExport::manualPrivateMediaAcceptance()
{
    const QString source = qEnvironmentVariable("ATK_PRIVATE_EXPORT_MEDIA");
    if (source.isEmpty()) QSKIP("Set ATK_PRIVATE_EXPORT_MEDIA for local acceptance media");
    QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
    const QList<QPair<qint64, qint64>> ranges{{45, 75}, {45, 74}, {10, 10}};
    for (const auto& range : ranges) {
        const QString output = QDir(directory.path()).filePath(
            QStringLiteral("private_%1_%2.mp4").arg(range.first).arg(range.second));
        const auto spec = specFor(source, output, range.first, range.second);
        QCOMPARE(spec.sourceA.metadata.frameRate, (media::FrameRate{24, 1}));
        QVERIFY2(runExport(spec, &error), qPrintable(error));
        verifyTiming(inspectOutput(output, &error), spec.frameCount(), 24, 1);
    }
    for (const auto layout : {playback::CompareLayout::SideBySide, playback::CompareLayout::Difference}) {
        auto spec = specFor(source, QDir(directory.path()).filePath(
            QStringLiteral("private_compare_%1.mp4").arg(static_cast<int>(layout))), 45, 75);
        spec.comparison = true; spec.layout = layout; spec.sourceB = spec.sourceA;
        spec.sourceB.rangeStartFrame = 45; spec.sourceB.rangeEndFrame = spec.sourceA.metadata.effectiveFrameCount() - 1;
        QVERIFY2(runExport(spec, &error), qPrintable(error));
        const OutputStats stats = inspectOutput(spec.outputPath, &error); verifyTiming(stats, 31, 24, 1);
        if (layout == playback::CompareLayout::Difference)
            QVERIFY(stats.firstImage.pixelColor(stats.firstImage.width() / 2, stats.firstImage.height() / 2).value() < 8);
    }
}

QTEST_MAIN(TestExport)
#include "tst_export.moc"
