#include "annotation/AnnotationTypes.h"
#include "export/ExportBurnInRenderer.h"
#include "export/ExportJob.h"
#include "export/ExportRenderer.h"
#include "export/FFmpegExporter.h"
#include "media/MediaDecoder.h"

#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace atk;

namespace {
QString fixture() { return QDir(QStringLiteral(ATK_TEST_MEDIA_DIR)).filePath(QStringLiteral("atk_export_120f.mkv")); }

exporter::ExportSpec specFor(qint64 first, qint64 last)
{
    media::MediaDecoder decoder;
    QString error;
    if (!decoder.open(fixture(), &error)) return {};
    exporter::ExportSpec spec;
    spec.sourceA.path = fixture();
    spec.sourceA.metadata = decoder.metadata();
    spec.sourceA.rangeStartFrame = first;
    spec.sourceA.rangeEndFrame = last;
    spec.videoEncoder = exporter::FFmpegExporter::availableH264Encoder();
    return spec;
}

/// An unusual exact colour the test clip never contains, so a pixel that has
/// it can only be the burned-in drawing.
const QColor kMarker(13, 251, 187);

/// A big solid marker dot in the top-left quarter, so it is easy to find.
annotation::AnnotationItem greenDot(quint64 id)
{
    annotation::AnnotationItem dot;
    dot.id = id;
    dot.tool = annotation::AnnotationTool::Marker;
    dot.color = kMarker;
    dot.size = 0.2;
    dot.points = {{0.25f, 0.25f, 1, 0, 0}};
    return dot;
}

annotation::SourceAnnotations annotationsAt(std::initializer_list<qint64> frames, bool withNote)
{
    annotation::SourceAnnotations set;
    quint64 id = 1;
    for (qint64 f : frames) {
        annotation::FrameAnnotation frame;
        frame.frame = f;
        frame.items = {greenDot(id++)};
        if (withNote) frame.notes = {{id++, QStringLiteral("fix the nose"), QStringLiteral("David"), 0, false}};
        set.setFrame(frame);
    }
    return set;
}

bool isGreen(const QImage& image)
{
    if (image.isNull()) return false;
    const QColor c = image.pixelColor(image.width() / 4, image.height() / 4);
    return c.red() == kMarker.red() && c.green() == kMarker.green() && c.blue() == kMarker.blue();
}

QImage renderFrame(const exporter::ExportSpec& spec, qint64 frame)
{
    exporter::ExportRenderer renderer;
    QString error;
    if (!renderer.open(spec, &error)) return {};
    exporter::RenderedExportFrame out;
    if (!renderer.render(frame, out, &error)) return {};
    return out.image;
}

/// Mean brightness of the note card area (bottom right).
double cardDarkness(const QImage& image)
{
    const QRect area = exporter::ExportBurnInRenderer::noteBounds(image.size());
    double sum = 0;
    int n = 0;
    for (int y = area.bottom() - 6; y > area.bottom() - 14; --y)
        for (int x = area.left() + area.width() / 2; x < area.right() - 6; x += 3) {
            sum += qGray(image.pixel(x, y));
            ++n;
        }
    return n ? sum / n : 255.0;
}
} // namespace

class TestAnnotationExport : public QObject {
    Q_OBJECT
private slots:
    void drawingsAppearOnlyOnTheirFrame();
    void holdKeepsDrawingsUntilTheNextAnnotatedFrame();
    void notesBecomeACaption();
    void annotationsOffOrComparisonLeaveFramesClean();
    void imageSequenceExportBurnsInDrawings();
    void freezeRepeatsEachAnnotatedFrame();
    void freezeSplicesSilenceIntoTheSound();
    void tracingPaperWashesExportedFrames();
};

void TestAnnotationExport::drawingsAppearOnlyOnTheirFrame()
{
    exporter::ExportSpec spec = specFor(0, 119);
    QVERIFY(!spec.sourceA.path.isEmpty());
    spec.annotations = annotationsAt({10}, false);
    spec.burnIns.annotations = true;
    QVERIFY(isGreen(renderFrame(spec, 10)));
    QVERIFY(!isGreen(renderFrame(spec, 9)));
    QVERIFY(!isGreen(renderFrame(spec, 11))); // play-through: one frame only
}

void TestAnnotationExport::holdKeepsDrawingsUntilTheNextAnnotatedFrame()
{
    exporter::ExportSpec spec = specFor(0, 119);
    spec.annotations = annotationsAt({10, 13}, false);
    spec.burnIns.annotations = true;
    spec.burnIns.annotationHoldFrames = 5;
    QVERIFY(!isGreen(renderFrame(spec, 9)));
    for (qint64 f : {10, 11, 12, 13, 18}) QVERIFY2(isGreen(renderFrame(spec, f)), qPrintable(QString::number(f)));
    QVERIFY(!isGreen(renderFrame(spec, 19))); // 13 + 5 is the last held frame
    // Which annotation is shown: 12 still belongs to frame 10, 13 is its own.
    QCOMPARE(exporter::ExportBurnInRenderer::annotationFor(spec, 12)->frame, qint64(10));
    QCOMPARE(exporter::ExportBurnInRenderer::annotationFor(spec, 14)->frame, qint64(13));
    QVERIFY(exporter::ExportBurnInRenderer::annotationFor(spec, 30) == nullptr);
}

void TestAnnotationExport::notesBecomeACaption()
{
    exporter::ExportSpec spec = specFor(0, 119);
    spec.annotations = annotationsAt({10}, true);
    spec.burnIns.annotationNotes = true; // notes on, drawings off
    const QImage withNote = renderFrame(spec, 10);
    const QImage without = renderFrame(spec, 9);
    QVERIFY(!isGreen(withNote));
    QVERIFY2(cardDarkness(withNote) < cardDarkness(without) - 20,
             qPrintable(QStringLiteral("%1 vs %2").arg(cardDarkness(withNote)).arg(cardDarkness(without))));
}

void TestAnnotationExport::annotationsOffOrComparisonLeaveFramesClean()
{
    exporter::ExportSpec spec = specFor(0, 119);
    spec.annotations = annotationsAt({10}, true);
    QVERIFY(!isGreen(renderFrame(spec, 10))); // options off
    spec.burnIns.annotations = true;
    spec.comparison = true; // annotations are Source A only, not burned into composites
    QVERIFY(exporter::ExportBurnInRenderer::annotationFor(spec, 10) == nullptr);
}

void TestAnnotationExport::imageSequenceExportBurnsInDrawings()
{
    QTemporaryDir dir;
    exporter::ExportSpec spec = specFor(0, 119);
    spec.kind = exporter::ExportKind::ImageSequence;
    spec.firstFrame = 8;
    spec.lastFrame = 12;
    spec.imagePrefix = QStringLiteral("shot");
    spec.outputPath = dir.filePath(QStringLiteral("shot_frames"));
    spec.annotations = annotationsAt({10}, true);
    spec.burnIns.annotations = true;
    spec.burnIns.annotationNotes = true;
    QVERIFY2(spec.validate().isEmpty(), qPrintable(spec.validate()));
    exporter::ExportJob job(spec);
    QSignalSpy completed(&job, &exporter::ExportJob::completed);
    QSignalSpy failed(&job, &exporter::ExportJob::failed);
    job.start();
    QElapsedTimer timer;
    timer.start();
    while (completed.isEmpty() && failed.isEmpty() && timer.elapsed() < 60000) completed.wait(100);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
    QCOMPARE(completed.count(), 1);
    const QDir out(spec.outputPath);
    const QStringList files = out.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    QCOMPARE(files.size(), 5);
    // One-based names: frame index 10 is shot_0011.png.
    QVERIFY(isGreen(QImage(out.filePath(QStringLiteral("shot_0011.png")))));
    QVERIFY(!isGreen(QImage(out.filePath(QStringLiteral("shot_0010.png")))));
}

QTEST_MAIN(TestAnnotationExport)
void TestAnnotationExport::freezeRepeatsEachAnnotatedFrame()
{
    exporter::ExportSpec spec = specFor(0, 119);
    spec.annotations = annotationsAt({10, 40}, false);
    spec.burnIns.annotations = true;
    using Renderer = exporter::ExportBurnInRenderer;
    QCOMPARE(Renderer::videoSchedule(spec).size(), 120); // no freeze set: play through

    spec.burnIns.annotationFreezeFrames = 5;
    const auto schedule = Renderer::videoSchedule(spec);
    QCOMPARE(schedule.size(), 130);
    QCOMPARE(schedule[10].sourceFrame, qint64(10));
    QVERIFY(!schedule[10].repeat);
    for (int i = 11; i <= 15; ++i) {
        QCOMPARE(schedule[i].sourceFrame, qint64(10)); // the picture holds still
        QVERIFY(schedule[i].repeat);
    }
    QCOMPARE(schedule[16].sourceFrame, qint64(11)); // then plays on
    QVERIFY(!schedule[16].repeat);
    QCOMPARE(schedule.last().sourceFrame, qint64(119));

    // Only frames that are actually burned in freeze.
    spec.burnIns.annotations = false;
    QCOMPARE(Renderer::videoSchedule(spec).size(), 120);
    spec.annotations = annotationsAt({10}, true); // drawing + note
    spec.burnIns.annotationNotes = true;          // notes still on
    QCOMPARE(Renderer::videoSchedule(spec).size(), 125);
    // Frames inside a hold do not freeze again; only the drawn frame does.
    spec.burnIns.annotationHoldFrames = 12;
    QCOMPARE(Renderer::videoSchedule(spec).size(), 125);
    // Nothing outside the export range.
    spec.sourceA.rangeStartFrame = 20;
    QCOMPARE(Renderer::videoSchedule(spec).size(), 100);
}

void TestAnnotationExport::freezeSplicesSilenceIntoTheSound()
{
    using Renderer = exporter::ExportBurnInRenderer;
    const media::FrameRate rate{24, 1};
    const int sampleRate = 48000; // 2000 samples per frame
    const int bytesPerSample = 4; // stereo 16-bit
    const qint64 perFrame = 2000 * bytesPerSample;
    // Four source frames of sound, each a distinct non-zero byte.
    QByteArray source;
    for (char f = 1; f <= 4; ++f) source.append(QByteArray(perFrame, f));
    const QVector<Renderer::ScheduledFrame> schedule{
        {0, false}, {1, false}, {1, true}, {1, true}, {2, false}, {3, false}};
    const QByteArray out = Renderer::scheduleAudio(source, schedule, rate, sampleRate, bytesPerSample);
    QCOMPARE(out.size(), 6 * perFrame);
    const char expected[] = {1, 2, 0, 0, 3, 4}; // silence while frozen, then the rest in step
    for (int j = 0; j < 6; ++j) {
        const QByteArray slice = out.mid(j * perFrame, perFrame);
        QCOMPARE(slice, QByteArray(perFrame, expected[j]));
    }
    // A fractional rate keeps the exact sample grid (29.97: 1601.6 samples a frame).
    const media::FrameRate ntsc{30000, 1001};
    QCOMPARE(Renderer::samplesAtFrame(5, ntsc, sampleRate), qint64(8008));
    const QByteArray ntscOut = Renderer::scheduleAudio(QByteArray(8008 * bytesPerSample, 7), {{0, false}, {0, true},
        {1, false}, {2, false}, {3, false}, {4, false}}, ntsc, sampleRate, bytesPerSample);
    QCOMPARE(ntscOut.size(), qsizetype(Renderer::samplesAtFrame(6, ntsc, sampleRate) * bytesPerSample));
}

void TestAnnotationExport::tracingPaperWashesExportedFrames()
{
    exporter::ExportSpec spec = specFor(0, 119);
    spec.annotations = annotationsAt({10}, false);
    spec.burnIns.annotations = true;
    const QImage plain = renderFrame(spec, 10);
    spec.burnIns.tracingPaperPercent = 50;
    const QImage traced = renderFrame(spec, 10);
    QVERIFY(isGreen(traced)); // drawings sit on top of the paper, unchanged
    // Elsewhere the picture is half-way to white.
    const QPoint probe(plain.width() * 3 / 4, plain.height() * 3 / 4);
    const QColor before = plain.pixelColor(probe);
    const QColor after = traced.pixelColor(probe);
    QVERIFY(qAbs(after.red() - (before.red() + 255) / 2) <= 3);
    QVERIFY(qAbs(after.green() - (before.green() + 255) / 2) <= 3);
    QVERIFY(qAbs(after.blue() - (before.blue() + 255) / 2) <= 3);
}

#include "tst_annotationexport.moc"
