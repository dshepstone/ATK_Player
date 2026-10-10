#include "annotation/AbrReader.h"
#include "annotation/AnnotationRenderer.h"
#include "annotation/BrushLibrary.h"
#include "annotation/BrushTip.h"

#include <QDir>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <cstring>

using namespace atk::annotation;

namespace {
/// Builds .abr bytes the way Photoshop lays them out (big-endian).
struct Writer {
    QByteArray b;
    void u8(int v) { b.append(char(v)); }
    void u16(int v) { u8((v >> 8) & 0xff); u8(v & 0xff); }
    void u32(quint32 v) { u16(int(v >> 16)); u16(int(v & 0xffff)); }
    void i32(qint32 v) { u32(quint32(v)); }
    void f64(double d) { quint64 bits; std::memcpy(&bits, &d, 8); u32(quint32(bits >> 32)); u32(quint32(bits)); }
    void raw(const QByteArray& a) { b.append(a); }
    void unicode(const QString& s) { u32(quint32(s.size() + 1)); for (QChar c : s) u16(c.unicode()); u16(0); }
    void key(const char* k) { const int n = int(strlen(k)); if (n == 4) { u32(0); raw(k); } else { u32(quint32(n)); raw(k); } }
};

/// A w x h tip: a filled disc of ink on transparent paper.
QImage disc(int w, int h)
{
    QImage img(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double dx = (x + 0.5 - w / 2.0) / (w / 2.0), dy = (y + 0.5 - h / 2.0) / (h / 2.0);
            img.scanLine(y)[x] = uchar(dx * dx + dy * dy <= 0.8 ? 255 - (x % 7) : 0);
        }
    return img;
}

QByteArray packBitsRow(const QByteArray& row)
{
    // Simple but valid encoder: literal runs of up to 128 and repeats of 3+.
    QByteArray out;
    int i = 0;
    while (i < row.size()) {
        int run = 1;
        while (i + run < row.size() && row[i + run] == row[i] && run < 128) ++run;
        if (run >= 3) {
            out.append(char(1 - run));
            out.append(row[i]);
            i += run;
            continue;
        }
        int start = i, len = 0;
        while (i < row.size() && len < 128) {
            int r = 1;
            while (i + r < row.size() && row[i + r] == row[i] && r < 3) ++r;
            if (r >= 3) break;
            ++i;
            ++len;
        }
        out.append(char(len - 1));
        out.append(row.mid(start, len));
    }
    return out;
}

/// Writes bounds, depth, compression and pixel data for `img`.
void writeImage(Writer& w, const QImage& img, int depth, bool rle)
{
    w.i32(0); w.i32(0); w.i32(img.height()); w.i32(img.width());
    w.u16(depth);
    w.u8(rle ? 1 : 0);
    QVector<QByteArray> rows;
    for (int y = 0; y < img.height(); ++y) {
        QByteArray row;
        for (int x = 0; x < img.width(); ++x) {
            row.append(char(img.constScanLine(y)[x]));
            if (depth == 16) row.append(char(0x55)); // low byte, ignored
        }
        rows.append(row);
    }
    if (!rle) {
        for (const QByteArray& row : rows) w.raw(row);
        return;
    }
    QVector<QByteArray> packed;
    for (const QByteArray& row : rows) packed.append(packBitsRow(row));
    for (const QByteArray& p : packed) w.u16(int(p.size()));
    for (const QByteArray& p : packed) w.raw(p);
}

QByteArray makeV2(const QImage& a, const QImage& b)
{
    Writer w;
    w.u16(2);
    w.u16(3);
    // A computed (round) brush: has no image, must be skipped and counted.
    w.u16(1); w.u32(14); for (int i = 0; i < 14; ++i) w.u8(0);
    for (int n = 0; n < 2; ++n) {
        Writer body;
        body.u32(0);                // misc
        body.u16(n == 0 ? 40 : 0);  // spacing %
        body.unicode(n == 0 ? QStringLiteral("Chalk") : QString());
        body.u8(1);                 // antialias
        for (int i = 0; i < 4; ++i) body.u16(0);
        writeImage(body, n == 0 ? a : b, 8, n == 1);
        w.u16(2);
        w.u32(quint32(body.b.size()));
        w.raw(body.b);
    }
    return w.b;
}

QByteArray sampleBlock(const QByteArray& uuid, const QImage& img, int subversion, int depth, bool rle)
{
    Writer s;
    s.u8(uuid.size());
    s.raw(uuid);
    for (int i = 0; i < (subversion == 1 ? 10 : 264); ++i) s.u8(0);
    writeImage(s, img, depth, rle);
    Writer w;
    w.u32(quint32(s.b.size()));
    w.raw(s.b);
    while (w.b.size() % 4) w.u8(0);
    return w.b;
}

QByteArray descSection(bool unsupportedTail)
{
    Writer d;
    d.u32(16);
    d.unicode(QString()); d.key("null");
    d.u32(1);
    d.key("Brsh"); d.raw("VlLs"); d.u32(unsupportedTail ? 3 : 2);
    const char* names[2] = {"Dry Ink", "Spatter"};
    const char* uuids[2] = {"$1111-aaaa", "2222-bbbb"}; // one with Photoshop's "$" prefix
    for (int i = 0; i < 2; ++i) {
        d.raw("Objc"); d.unicode(QString()); d.key("brushPreset"); d.u32(2);
        d.key("Nm  "); d.raw("TEXT"); d.unicode(QString::fromLatin1(names[i]));
        d.key("Brsh"); d.raw("Objc"); d.unicode(QString()); d.key("sampledBrush"); d.u32(4);
        d.key("Dmtr"); d.raw("UntF"); d.raw("#Pxl"); d.f64(i == 0 ? 120.0 : 60.0);
        d.key("Spcn"); d.raw("UntF"); d.raw("#Prc"); d.f64(i == 0 ? 50.0 : 15.0);
        d.key("Angl"); d.raw("UntF"); d.raw("#Ang"); d.f64(i == 0 ? 30.0 : 0.0);
        d.key("sampledData"); d.raw("TEXT"); d.unicode(QString::fromLatin1(uuids[i]));
    }
    if (unsupportedTail) d.raw("obj "); // a reference: parsing stops here, presets before it stay
    return d.b;
}

QByteArray makeV6(int version, int subversion, const QImage& a, const QImage& b, bool withDesc,
                  bool unsupportedTail = false)
{
    Writer w;
    w.u16(version);
    w.u16(subversion);
    QByteArray samp = sampleBlock("1111-aaaa", a, subversion, 8, false)
        + sampleBlock("$2222-bbbb", b, subversion, 16, true);
    w.raw("8BIM"); w.raw("samp"); w.u32(quint32(samp.size())); w.raw(samp);
    w.raw("8BIM"); w.raw("patt"); w.u32(0);
    if (withDesc) {
        const QByteArray desc = descSection(unsupportedTail);
        w.raw("8BIM"); w.raw("desc"); w.u32(quint32(desc.size())); w.raw(desc);
    }
    return w.b;
}

bool sameMask(const QImage& a, const QImage& b)
{
    if (a.size() != b.size()) return false;
    for (int y = 0; y < a.height(); ++y)
        if (std::memcmp(a.constScanLine(y), b.constScanLine(y), size_t(a.width())) != 0) return false;
    return true;
}
} // namespace

class TestAbrReader : public QObject {
    Q_OBJECT
private slots:
    void readsVersion2RawAndCompressed();
    void readsVersion6WithPresets_data();
    void readsVersion6WithPresets();
    void presetsBeforeAnUnsupportedValueSurvive();
    void invertedTipsAreDetected();
    void rejectsUnsupportedAndEmptyFiles();
    void computedOnlySetsExplainThemselves();
    void truncatedAndCorruptFilesNeverCrash();
    void libraryRoundTripsSetsOnDisk();
    void registryLooksUpById();
};

void TestAbrReader::readsVersion2RawAndCompressed()
{
    const QImage a = disc(24, 16), b = disc(9, 31);
    const AbrReadResult result = readAbr(makeV2(a, b), QStringLiteral("Set"));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.skippedComputed, 1);
    QCOMPARE(result.tips.size(), 2);
    QCOMPARE(result.tips[0].name, QStringLiteral("Chalk"));
    QCOMPARE(result.tips[1].name, QStringLiteral("Set 2"));
    QVERIFY(std::abs(result.tips[0].spacing - 0.40) < 1e-9);
    QVERIFY(std::abs(result.tips[1].spacing - 0.25) < 1e-9); // 0 means "default"
    QVERIFY(sameMask(result.tips[0].mask, a));
    QVERIFY(sameMask(result.tips[1].mask, b)); // PackBits decoded exactly
    QVERIFY(result.tips[0].id.startsWith(QStringLiteral("sha1-")));
    QVERIFY(result.tips[0].id != result.tips[1].id);
}

void TestAbrReader::readsVersion6WithPresets_data()
{
    QTest::addColumn<int>("version");
    QTest::addColumn<int>("subversion");
    QTest::newRow("v6.1") << 6 << 1;
    QTest::newRow("v6.2") << 6 << 2;
    QTest::newRow("v7.2") << 7 << 2;
    QTest::newRow("v10.2") << 10 << 2;
}

void TestAbrReader::readsVersion6WithPresets()
{
    QFETCH(int, version);
    QFETCH(int, subversion);
    const QImage a = disc(40, 40), b = disc(17, 23);
    const AbrReadResult result = readAbr(makeV6(version, subversion, a, b, true));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.tips.size(), 2);
    QVERIFY(sameMask(result.tips[0].mask, a));
    QVERIFY(sameMask(result.tips[1].mask, b)); // 16-bit, RLE: high bytes kept
    QCOMPARE(result.tips[0].name, QStringLiteral("Dry Ink"));
    QCOMPARE(result.tips[1].name, QStringLiteral("Spatter")); // matched despite the "$" prefix
    QCOMPARE(result.tips[0].id, QStringLiteral("abr-1111-aaaa"));
    QCOMPARE(result.tips[1].id, QStringLiteral("abr-2222-bbbb"));
    QVERIFY(std::abs(result.tips[0].spacing - 0.50) < 1e-9);
    QVERIFY(std::abs(result.tips[1].spacing - 0.15) < 1e-9);
    QCOMPARE(result.tips[0].angle, 30.0);
    QCOMPARE(result.tips[0].diameter, 120);

    // Without presets, tips still import with generated names.
    const AbrReadResult bare = readAbr(makeV6(version, subversion, a, b, false), QStringLiteral("Kit"));
    QCOMPARE(bare.tips.size(), 2);
    QCOMPARE(bare.tips[0].name, QStringLiteral("Kit 1"));
}

void TestAbrReader::presetsBeforeAnUnsupportedValueSurvive()
{
    const AbrReadResult result = readAbr(makeV6(6, 2, disc(10, 10), disc(12, 12), true, true));
    QCOMPARE(result.tips.size(), 2);
    QCOMPARE(result.tips[0].name, QStringLiteral("Dry Ink"));
    QCOMPARE(result.tips[1].name, QStringLiteral("Spatter"));
}

void TestAbrReader::invertedTipsAreDetected()
{
    QImage inverted = disc(20, 20);
    inverted.invertPixels(); // paper bright, ink dark
    const AbrReadResult result = readAbr(makeV2(inverted, disc(8, 8)));
    QVERIFY(result.ok());
    QImage expected = inverted;
    expected.invertPixels();
    QVERIFY(sameMask(result.tips[0].mask, expected));
    QCOMPARE(result.tips[0].mask.constScanLine(0)[0], uchar(0)); // corner is paper
}

void TestAbrReader::rejectsUnsupportedAndEmptyFiles()
{
    QVERIFY(!readAbr(QByteArray()).ok());
    QVERIFY(!readAbr(QByteArray("\x00", 1)).ok());
    Writer w;
    w.u16(5);
    w.u16(1);
    const AbrReadResult v5 = readAbr(w.b);
    QVERIFY(!v5.ok());
    QVERIFY(v5.error.contains(QStringLiteral("version 5")));
    Writer bad;
    bad.u16(6);
    bad.u16(9);
    QVERIFY(!readAbr(bad.b).ok());
    QVERIFY(!readAbr(QByteArray("PNG not a brush set")).ok());
}

void TestAbrReader::computedOnlySetsExplainThemselves()
{
    Writer w;
    w.u16(1);
    w.u16(1);
    w.u16(1); w.u32(14); for (int i = 0; i < 14; ++i) w.u8(0);
    const AbrReadResult result = readAbr(w.b);
    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("round brushes")));
}

void TestAbrReader::truncatedAndCorruptFilesNeverCrash()
{
    const QList<QByteArray> valid = {makeV2(disc(24, 16), disc(9, 31)),
                                     makeV6(6, 2, disc(30, 30), disc(17, 23), true)};
    for (const QByteArray& file : valid) {
        // Every truncation: a clean result or a clean error, never a crash.
        for (int n = 0; n < file.size(); n += 3) {
            const AbrReadResult r = readAbr(file.left(n));
            for (const BrushTip& tip : r.tips) QVERIFY(tip.isValid());
        }
        // Random corruption.
        QRandomGenerator rng(1234);
        for (int i = 0; i < 1500; ++i) {
            QByteArray mutated = file;
            for (int k = 0; k < 6; ++k) mutated[int(rng.bounded(mutated.size()))] = char(rng.bounded(256));
            const AbrReadResult r = readAbr(mutated);
            for (const BrushTip& tip : r.tips) {
                QVERIFY(tip.mask.width() <= 4096 && tip.mask.height() <= 4096);
            }
        }
    }
    // Absurd image bounds are refused instead of allocating.
    Writer w;
    w.u16(2); w.u16(1);
    Writer body;
    body.u32(0); body.u16(25); body.unicode(QStringLiteral("Huge")); body.u8(1);
    for (int i = 0; i < 4; ++i) body.u16(0);
    body.i32(0); body.i32(0); body.i32(100000); body.i32(100000); body.u16(8); body.u8(0);
    w.u16(2); w.u32(quint32(body.b.size())); w.raw(body.b);
    QVERIFY(!readAbr(w.b).ok());
}

void TestAbrReader::libraryRoundTripsSetsOnDisk()
{
    QTemporaryDir dir;
    const AbrReadResult read = readAbr(makeV6(6, 2, disc(30, 30), disc(17, 23), true));
    BrushSet set;
    set.name = QStringLiteral("My: Brushes/1");
    set.tips = read.tips;
    {
        BrushLibrary library(dir.path());
        QString error;
        QVERIFY2(library.addSet(set, &error), qPrintable(error));
        QCOMPARE(library.sets().size(), 1);
    }
    BrushLibrary reloaded(dir.path());
    reloaded.load();
    QCOMPARE(reloaded.sets().size(), 1);
    QCOMPARE(reloaded.sets()[0].name, set.name);
    QCOMPARE(reloaded.sets()[0].tips.size(), 2);
    for (int i = 0; i < 2; ++i) {
        const BrushTip& t = reloaded.sets()[0].tips[i];
        QCOMPARE(t.id, set.tips[i].id);
        QCOMPARE(t.name, set.tips[i].name);
        QCOMPARE(t.spacing, set.tips[i].spacing);
        QVERIFY(sameMask(t.mask, set.tips[i].mask)); // PNG is lossless
    }
    QVERIFY(reloaded.findTip(set.tips[1].id).isValid());
    // The folder name is made safe for every file system.
    QVERIFY(!QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).constFirst().contains(QLatin1Char(':')));
    QVERIFY(reloaded.removeSet(set.name));
    BrushLibrary empty(dir.path());
    empty.load();
    QVERIFY(empty.sets().isEmpty());
}

void TestAbrReader::registryLooksUpById()
{
    BrushTip tip;
    tip.id = QStringLiteral("test-registry-tip");
    tip.mask = disc(8, 8);
    QVERIFY(!BrushTipRegistry::instance().contains(tip.id));
    BrushTipRegistry::instance().add(tip);
    QVERIFY(sameMask(BrushTipRegistry::instance().find(tip.id).mask, tip.mask));
    QVERIFY(!BrushTipRegistry::instance().find(QStringLiteral("nope")).isValid());
    BrushTipRegistry::instance().add(BrushTip{}); // invalid tips are ignored
}

QTEST_GUILESS_MAIN(TestAbrReader)
#include "tst_abrreader.moc"
