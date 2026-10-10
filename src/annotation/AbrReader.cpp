#include "annotation/AbrReader.h"

#include <QHash>

#include <algorithm>
#include <cstring>

namespace atk::annotation {
namespace {

constexpr int kMaxTipSide = 4096;
constexpr int kMaxTips = 2000;
constexpr int kMaxDescriptorDepth = 32;

/// Big-endian reader that never reads past the end; any overrun sets `ok`
/// false and returns zeros, so parsing code can check once per record.
class Reader {
public:
    explicit Reader(const QByteArray& data) : m_data(data) {}
    bool ok = true;
    qsizetype pos = 0;
    qsizetype size() const { return m_data.size(); }
    qsizetype remaining() const { return std::max<qsizetype>(0, m_data.size() - pos); }
    bool have(qsizetype n) { if (n < 0 || remaining() < n) { ok = false; return false; } return true; }

    quint8 u8() { if (!have(1)) return 0; return quint8(m_data[pos++]); }
    quint16 u16() { if (!have(2)) return 0; const quint16 v = quint16((quint8(m_data[pos]) << 8) | quint8(m_data[pos + 1])); pos += 2; return v; }
    quint32 u32() { if (!have(4)) return 0; quint32 v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | quint8(m_data[pos + i]); pos += 4; return v; }
    qint32 i32() { return qint32(u32()); }
    quint64 u64() { const quint64 hi = u32(); return (hi << 32) | u32(); }
    double f64() { const quint64 bits = u64(); double d; std::memcpy(&d, &bits, sizeof d); return d; }
    QByteArray bytes(qsizetype n) { if (!have(n)) return {}; QByteArray b = m_data.mid(pos, n); pos += n; return b; }
    void skip(qsizetype n) { if (have(n)) pos += n; }
    QString unicode() // u32 length in UTF-16 code units, then UTF-16BE, possibly NUL-terminated
    {
        const quint32 count = u32();
        if (count > 1u << 20 || !have(qsizetype(count) * 2)) { ok = false; return {}; }
        QString s;
        s.reserve(int(count));
        for (quint32 i = 0; i < count; ++i) s.append(QChar(u16()));
        while (s.endsWith(QChar(0))) s.chop(1);
        return s;
    }
    QByteArray key() // descriptor key: u32 length, 0 means a 4-byte key
    {
        const quint32 length = u32();
        return bytes(length == 0 ? 4 : qsizetype(std::min<quint32>(length, 1u << 16)));
    }
    const QByteArray& data() const { return m_data; }

private:
    const QByteArray& m_data;
};

/// Unpacks PackBits data into exactly `out.size()` bytes.
bool unpackBits(Reader& r, qsizetype rowEnd, QByteArray& out)
{
    qsizetype filled = 0;
    while (filled < out.size() && r.pos < rowEnd && r.ok) {
        const qint8 n = qint8(r.u8());
        if (n >= 0) {
            const qsizetype count = qsizetype(n) + 1;
            if (filled + count > out.size() || !r.have(count)) return false;
            std::memcpy(out.data() + filled, r.data().constData() + r.pos, size_t(count));
            r.pos += count;
            filled += count;
        } else if (n != -128) {
            const qsizetype count = 1 - qsizetype(n);
            const char value = char(r.u8());
            if (filled + count > out.size()) return false;
            std::memset(out.data() + filled, value, size_t(count));
            filled += count;
        }
    }
    return r.ok && filled == out.size();
}

/// Reads one tip image (bounds, depth, compression, pixels) into a mask.
QImage readImage(Reader& r, qsizetype limit)
{
    const qint32 top = r.i32(), left = r.i32(), bottom = r.i32(), right = r.i32();
    const quint16 depth = r.u16();
    const quint8 compression = r.u8();
    if (!r.ok) return {};
    const qint64 width = qint64(right) - left;
    const qint64 height = qint64(bottom) - top;
    if (width <= 0 || height <= 0 || width > kMaxTipSide || height > kMaxTipSide) return {};
    if (depth != 8 && depth != 16) return {};
    const int bpp = depth / 8;
    const qsizetype rowBytes = qsizetype(width) * bpp;

    QImage mask(int(width), int(height), QImage::Format_Grayscale8);
    QByteArray row(rowBytes, '\0');
    if (compression == 0) {
        if (r.pos + rowBytes * height > limit) return {};
        for (int y = 0; y < height; ++y) {
            row = r.bytes(rowBytes);
            if (!r.ok) return {};
            uchar* line = mask.scanLine(y);
            for (int x = 0; x < width; ++x) line[x] = uchar(row[x * bpp]); // high byte of 16-bit
        }
    } else if (compression == 1) {
        QVector<quint16> counts(static_cast<int>(height));
        for (int y = 0; y < height; ++y) counts[y] = r.u16();
        if (!r.ok) return {};
        for (int y = 0; y < height; ++y) {
            const qsizetype rowEnd = std::min(limit, r.pos + qsizetype(counts[y]));
            row.fill('\0');
            if (!unpackBits(r, rowEnd, row)) return {};
            r.pos = rowEnd;
            uchar* line = mask.scanLine(y);
            for (int x = 0; x < width; ++x) line[x] = uchar(row[x * bpp]);
        }
    } else {
        return {};
    }

    // Tips are stored with ink as high values; a few writers store the
    // opposite. Tip edges are paper, so a bright border means inverted data.
    qint64 edge = 0, edgeCount = 0;
    for (int x = 0; x < width; ++x) {
        edge += mask.constScanLine(0)[x] + mask.constScanLine(int(height) - 1)[x];
        edgeCount += 2;
    }
    for (int y = 0; y < height; ++y) {
        edge += mask.constScanLine(y)[0] + mask.constScanLine(y)[int(width) - 1];
        edgeCount += 2;
    }
    if (edgeCount > 0 && edge / edgeCount > 160) mask.invertPixels();
    return mask;
}

// --- Version 1 and 2 -------------------------------------------------------

AbrReadResult readV12(Reader& r, quint16 version, const QString& prefix)
{
    AbrReadResult result;
    const quint16 count = r.u16();
    for (int i = 0; i < count && r.ok && result.tips.size() < kMaxTips; ++i) {
        const quint16 type = r.u16();
        const quint32 size = r.u32();
        const qsizetype end = r.pos + qsizetype(size);
        if (!r.ok || end > r.size()) break;
        if (type == 2) {
            r.u32(); // misc
            const quint16 spacing = r.u16();
            QString name;
            if (version == 2) name = r.unicode();
            r.u8();   // antialiasing
            r.skip(8); // short bounds, repeated below as longs
            BrushTip tip;
            tip.mask = readImage(r, end);
            if (r.ok && !tip.mask.isNull()) {
                tip.name = name.isEmpty() ? QStringLiteral("%1 %2").arg(prefix).arg(result.tips.size() + 1) : name;
                tip.spacing = spacing > 0 ? std::clamp(spacing / 100.0, 0.01, 10.0) : 0.25;
                tip.diameter = std::max(tip.mask.width(), tip.mask.height());
                tip.id = BrushTip::idForMask(tip.mask);
                result.tips.append(tip);
            }
        } else if (type == 1) {
            ++result.skippedComputed;
        }
        r.ok = true; // a bad brush should not hide the next one
        r.pos = end;
    }
    return result;
}

// --- Version 6+ descriptors ------------------------------------------------

struct DescValue {
    QByteArray type;
    double number = 0.0;
    QString text;
    QVector<QPair<QByteArray, DescValue>> fields; // Objc / GlbO
    QVector<DescValue> list;                      // VlLs

    const DescValue* field(const char* key) const
    {
        for (const auto& f : fields)
            if (f.first == key) return &f.second;
        return nullptr;
    }
};

bool readValue(Reader& r, DescValue& out, int depth);

bool readDescriptor(Reader& r, DescValue& out, int depth)
{
    if (depth > kMaxDescriptorDepth) return false;
    r.unicode(); // class name
    r.key();     // class id
    const quint32 count = r.u32();
    if (!r.ok || count > 100000) return false;
    for (quint32 i = 0; i < count && r.ok; ++i) {
        QByteArray key = r.key();
        DescValue value;
        value.type = r.bytes(4);
        // Keep what was read even if parsing stops here, so an unsupported
        // value late in the file does not lose the presets before it.
        const bool good = readValue(r, value, depth + 1);
        out.fields.append({key, std::move(value)});
        if (!good) return false;
    }
    return r.ok;
}

bool readValue(Reader& r, DescValue& v, int depth)
{
    if (depth > kMaxDescriptorDepth || !r.ok) return false;
    const QByteArray& t = v.type;
    if (t == "Objc" || t == "GlbO") return readDescriptor(r, v, depth);
    if (t == "VlLs") {
        const quint32 count = r.u32();
        if (count > 100000) return false;
        for (quint32 i = 0; i < count && r.ok; ++i) {
            DescValue item;
            item.type = r.bytes(4);
            const bool good = readValue(r, item, depth + 1);
            v.list.append(std::move(item));
            if (!good) return false;
        }
        return r.ok;
    }
    if (t == "doub") { v.number = r.f64(); return r.ok; }
    if (t == "UntF") { r.bytes(4); v.number = r.f64(); return r.ok; }
    if (t == "UnFl") { r.bytes(4); const quint32 n = r.u32(); if (n > 100000) return false; for (quint32 i = 0; i < n; ++i) v.number = r.f64(); return r.ok; }
    if (t == "TEXT") { v.text = r.unicode(); return r.ok; }
    if (t == "enum") { r.key(); v.text = QString::fromLatin1(r.key()); return r.ok; }
    if (t == "long") { v.number = r.i32(); return r.ok; }
    if (t == "comp") { v.number = double(qint64(r.u64())); return r.ok; }
    if (t == "bool") { v.number = r.u8(); return r.ok; }
    if (t == "type" || t == "GlbC") { r.unicode(); r.key(); return r.ok; }
    if (t == "alis" || t == "tdta") { r.skip(r.u32()); return r.ok; }
    return false; // references ('obj ') and anything unknown: stop, keep what we have
}

/// Photoshop writes the sample UUID with a "$" prefix in some places and not
/// in others; compare without it.
QString normalizedUuid(QString id)
{
    id = id.trimmed();
    while (id.startsWith(QLatin1Char('$'))) id.remove(0, 1);
    return id;
}

struct Preset {
    QString name;
    double spacing = -1; // percent
    double angle = 0;
    double diameter = 0;
};

/// Walks the "desc" section and maps sampled-tip UUIDs to their presets.
QHash<QString, Preset> readPresets(Reader& r, qsizetype end)
{
    QHash<QString, Preset> presets;
    r.u32(); // descriptor version (16)
    DescValue root;
    if (!readDescriptor(r, root, 0) && root.fields.isEmpty()) return presets;
    const DescValue* list = root.field("Brsh");
    if (!list) return presets;
    for (const DescValue& entry : list->list) {
        const DescValue* brush = entry.field("Brsh");
        const DescValue* name = entry.field("Nm  ");
        if (!brush) continue;
        const DescValue* uuid = brush->field("sampledData");
        if (!uuid || uuid->text.isEmpty()) continue;
        Preset preset;
        if (name) preset.name = name->text;
        if (const DescValue* s = brush->field("Spcn")) preset.spacing = s->number;
        if (const DescValue* a = brush->field("Angl")) preset.angle = a->number;
        if (const DescValue* d = brush->field("Dmtr")) preset.diameter = d->number;
        presets.insert(normalizedUuid(uuid->text), preset);
    }
    Q_UNUSED(end);
    return presets;
}

AbrReadResult readV6(Reader& r, const QString& prefix)
{
    AbrReadResult result;
    const quint16 subversion = r.u16();
    if (subversion != 1 && subversion != 2) {
        result.error = QStringLiteral("Unsupported Photoshop brush sub-version %1.").arg(subversion);
        return result;
    }
    QVector<QPair<QString, BrushTip>> samples;
    QHash<QString, Preset> presets;

    while (r.ok && r.remaining() >= 12) {
        // Sections may be padded; resynchronise on the next "8BIM".
        const qsizetype found = r.data().indexOf("8BIM", r.pos);
        if (found < 0) break;
        r.pos = found + 4;
        const QByteArray key = r.bytes(4);
        const quint32 length = r.u32();
        const qsizetype sectionEnd = std::min<qsizetype>(r.size(), r.pos + qsizetype(length));
        if (!r.ok) break;

        if (key == "samp") {
            while (r.ok && r.pos + 4 <= sectionEnd && samples.size() < kMaxTips) {
                const quint32 brushLength = r.u32();
                const qsizetype brushEnd = std::min<qsizetype>(sectionEnd, r.pos + ((qsizetype(brushLength) + 3) & ~qsizetype(3)));
                if (brushLength == 0) break;
                const quint8 idLength = r.u8();
                const QString id = QString::fromLatin1(r.bytes(idLength));
                r.skip(subversion == 1 ? 10 : 264);
                BrushTip tip;
                tip.mask = readImage(r, brushEnd);
                if (r.ok && !tip.mask.isNull()) {
                    tip.id = id.isEmpty() ? BrushTip::idForMask(tip.mask)
                                          : QStringLiteral("abr-") + normalizedUuid(id);
                    tip.diameter = std::max(tip.mask.width(), tip.mask.height());
                    samples.append({id, tip});
                }
                r.ok = true;
                r.pos = brushEnd;
            }
        } else if (key == "desc") {
            Reader sub(r.data());
            sub.pos = r.pos;
            presets = readPresets(sub, sectionEnd);
        }
        r.ok = true;
        r.pos = sectionEnd;
    }

    for (auto& [uuid, tip] : samples) {
        const Preset preset = presets.value(normalizedUuid(uuid));
        tip.name = preset.name.isEmpty() ? QStringLiteral("%1 %2").arg(prefix).arg(result.tips.size() + 1) : preset.name;
        if (preset.spacing > 0) tip.spacing = std::clamp(preset.spacing / 100.0, 0.01, 10.0);
        tip.angle = preset.angle;
        if (preset.diameter > 0) tip.diameter = int(std::round(preset.diameter));
        result.tips.append(tip);
    }
    return result;
}

} // namespace

AbrReadResult readAbr(const QByteArray& data, const QString& fallbackNamePrefix)
{
    AbrReadResult result;
    Reader r(data);
    const quint16 version = r.u16();
    if (!r.ok) {
        result.error = QStringLiteral("The file is empty or not a Photoshop brush set.");
        return result;
    }
    if (version == 1 || version == 2) result = readV12(r, version, fallbackNamePrefix);
    else if (version == 6 || version == 7 || version == 10) result = readV6(r, fallbackNamePrefix);
    else {
        result.error = QStringLiteral("This is not a supported Photoshop brush set (version %1).").arg(version);
        return result;
    }
    if (result.ok() && result.tips.isEmpty())
        result.error = result.skippedComputed > 0
            ? QStringLiteral("This brush set only contains round brushes defined by settings, which have no tip "
                             "image to import.")
            : QStringLiteral("No brush tips could be read from this file.");
    return result;
}

} // namespace atk::annotation
