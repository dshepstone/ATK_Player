#include "review/ZipArchive.h"

#include <algorithm>
#include <array>

namespace atk::review {
namespace {

// --- DEFLATE (RFC 1951), after the structure of Mark Adler's puff.c -------

constexpr int kMaxBits = 15;
constexpr int kMaxLitLen = 286;
constexpr int kMaxDist = 30;

struct BitReader {
    const uchar* data = nullptr;
    qsizetype size = 0;
    qsizetype pos = 0;
    quint32 bitBuffer = 0;
    int bitCount = 0;
    bool overrun = false;

    int bits(int need)
    {
        quint32 value = bitBuffer;
        while (bitCount < need) {
            if (pos >= size) {
                overrun = true;
                return 0;
            }
            value |= quint32(data[pos++]) << bitCount;
            bitCount += 8;
        }
        bitBuffer = value >> need;
        bitCount -= need;
        return int(value & ((1u << need) - 1));
    }
};

struct Huffman {
    std::array<short, kMaxBits + 1> count{};
    std::array<short, 288> symbol{}; // the fixed literal/length code has 288 symbols
};

/// Builds a canonical Huffman decoder from code lengths. Returns < 0 for an
/// over-subscribed set, 0 for complete, > 0 for incomplete.
int construct(Huffman& h, const short* lengths, int n)
{
    h.count.fill(0);
    for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
    if (h.count[0] == n) return 0; // no codes: complete, but decoding fails
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    std::array<short, kMaxBits + 1> offsets{};
    for (int len = 1; len < kMaxBits; ++len) offsets[len + 1] = short(offsets[len] + h.count[len]);
    for (int s = 0; s < n; ++s)
        if (lengths[s] != 0) h.symbol[offsets[lengths[s]]++] = short(s);
    return left;
}

int decode(BitReader& in, const Huffman& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= in.bits(1);
        if (in.overrun) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1; // ran out of codes
}

constexpr std::array<short, 29> kLengthBase{3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<short, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                             3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<short, 30> kDistBase{1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                          193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                          6145, 8193, 12289, 16385, 24577};
constexpr std::array<short, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(BitReader& in, QByteArray& out, const Huffman& lit, const Huffman& dist, qsizetype maxOutput)
{
    for (;;) {
        int symbol = decode(in, lit);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (out.size() >= maxOutput) return false;
            out.append(char(symbol));
        } else if (symbol == 256) {
            return true;
        } else {
            symbol -= 257;
            if (symbol >= 29) return false;
            const int length = kLengthBase[symbol] + in.bits(kLengthExtra[symbol]);
            const int ds = decode(in, dist);
            if (ds < 0 || ds >= 30 || in.overrun) return false;
            const qsizetype distance = kDistBase[ds] + in.bits(kDistExtra[ds]);
            if (in.overrun || distance > out.size() || out.size() + length > maxOutput) return false;
            const qsizetype from = out.size() - distance;
            for (int i = 0; i < length; ++i) out.append(out.at(from + i)); // may overlap: byte by byte
        }
    }
}

bool fixedBlock(BitReader& in, QByteArray& out, qsizetype maxOutput)
{
    static const auto tables = [] {
        std::pair<Huffman, Huffman> t;
        std::array<short, 288> lengths{};
        int s = 0;
        for (; s < 144; ++s) lengths[s] = 8;
        for (; s < 256; ++s) lengths[s] = 9;
        for (; s < 280; ++s) lengths[s] = 7;
        for (; s < 288; ++s) lengths[s] = 8;
        construct(t.first, lengths.data(), 288);
        std::array<short, kMaxDist> distLengths{};
        distLengths.fill(5);
        construct(t.second, distLengths.data(), kMaxDist);
        return t;
    }();
    return codes(in, out, tables.first, tables.second, maxOutput);
}

bool dynamicBlock(BitReader& in, QByteArray& out, qsizetype maxOutput)
{
    static constexpr std::array<short, 19> kOrder{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = in.bits(5) + 257;
    const int ndist = in.bits(5) + 1;
    const int ncode = in.bits(4) + 4;
    if (in.overrun || nlen > kMaxLitLen || ndist > kMaxDist) return false;
    std::array<short, kMaxLitLen + kMaxDist> lengths{};
    for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = short(in.bits(3));
    if (in.overrun) return false;
    Huffman lencode;
    if (construct(lencode, lengths.data(), 19) != 0) return false; // must be complete
    int index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(in, lencode);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[index++] = short(symbol);
            continue;
        }
        short value = 0;
        int repeat = 0;
        if (symbol == 16) {
            if (index == 0) return false;
            value = lengths[index - 1];
            repeat = 3 + in.bits(2);
        } else if (symbol == 17) {
            repeat = 3 + in.bits(3);
        } else {
            repeat = 11 + in.bits(7);
        }
        if (in.overrun || index + repeat > nlen + ndist) return false;
        while (repeat--) lengths[index++] = value;
    }
    if (lengths[256] == 0) return false; // no end-of-block code
    Huffman litcode, distcode;
    int err = construct(litcode, lengths.data(), nlen);
    if (err < 0 || (err > 0 && nlen - litcode.count[0] != 1)) return false;
    err = construct(distcode, lengths.data() + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;
    return codes(in, out, litcode, distcode, maxOutput);
}

bool storedBlock(BitReader& in, QByteArray& out, qsizetype maxOutput)
{
    in.bitBuffer = 0; // to a byte boundary
    in.bitCount = 0;
    if (in.pos + 4 > in.size) return false;
    const int len = in.data[in.pos] | (in.data[in.pos + 1] << 8);
    const int nlen = in.data[in.pos + 2] | (in.data[in.pos + 3] << 8);
    in.pos += 4;
    if (len != (~nlen & 0xffff) || in.pos + len > in.size || out.size() + len > maxOutput) return false;
    out.append(reinterpret_cast<const char*>(in.data + in.pos), len);
    in.pos += len;
    return true;
}

quint16 u16(const QByteArray& d, qsizetype at)
{
    return quint16(uchar(d[at]) | (uchar(d[at + 1]) << 8));
}

quint32 u32(const QByteArray& d, qsizetype at)
{
    return quint32(u16(d, at)) | (quint32(u16(d, at + 2)) << 16);
}

} // namespace

std::optional<QByteArray> inflateRaw(const QByteArray& deflated, qsizetype maxOutput)
{
    BitReader in;
    in.data = reinterpret_cast<const uchar*>(deflated.constData());
    in.size = deflated.size();
    QByteArray out;
    int last = 0;
    do {
        last = in.bits(1);
        const int type = in.bits(2);
        if (in.overrun) return std::nullopt;
        bool ok = false;
        switch (type) {
        case 0: ok = storedBlock(in, out, maxOutput); break;
        case 1: ok = fixedBlock(in, out, maxOutput); break;
        case 2: ok = dynamicBlock(in, out, maxOutput); break;
        default: ok = false;
        }
        if (!ok || in.overrun) return std::nullopt;
    } while (!last);
    return out;
}

bool ZipArchive::open(QByteArray data)
{
    m_entries.clear();
    m_data = std::move(data);
    // End of central directory: 22 bytes plus up to 64 KiB of comment.
    const qsizetype minEnd = 22;
    if (m_data.size() < minEnd) return false;
    qsizetype eocd = -1;
    for (qsizetype at = m_data.size() - minEnd; at >= 0 && at >= m_data.size() - minEnd - 0xFFFF; --at) {
        if (u32(m_data, at) == 0x06054b50) {
            eocd = at;
            break;
        }
    }
    if (eocd < 0) return false;
    const quint16 count = u16(m_data, eocd + 10);
    qsizetype at = u32(m_data, eocd + 16);
    for (int i = 0; i < count; ++i) {
        if (at + 46 > m_data.size() || u32(m_data, at) != 0x02014b50) return false;
        Entry entry;
        entry.method = u16(m_data, at + 10);
        entry.compressedSize = u32(m_data, at + 20);
        entry.uncompressedSize = u32(m_data, at + 24);
        const quint16 nameLength = u16(m_data, at + 28);
        const quint16 extraLength = u16(m_data, at + 30);
        const quint16 commentLength = u16(m_data, at + 32);
        entry.localHeaderOffset = u32(m_data, at + 42);
        if (at + 46 + nameLength > m_data.size()) return false;
        const QString name = QString::fromUtf8(m_data.mid(at + 46, nameLength));
        m_entries.insert(name, entry);
        at += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

std::optional<QByteArray> ZipArchive::read(const QString& name) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.cend()) return std::nullopt;
    const qsizetype local = it->localHeaderOffset;
    if (local + 30 > m_data.size() || u32(m_data, local) != 0x04034b50) return std::nullopt;
    const qsizetype start = local + 30 + u16(m_data, local + 26) + u16(m_data, local + 28);
    if (start + qsizetype(it->compressedSize) > m_data.size()) return std::nullopt;
    const QByteArray raw = m_data.mid(start, it->compressedSize);
    if (it->method == 0) return raw;
    if (it->method != 8) return std::nullopt; // only stored and deflated
    auto inflated = inflateRaw(raw, std::max<qsizetype>(qsizetype(it->uncompressedSize), 1) + 1024);
    if (!inflated || inflated->size() != qsizetype(it->uncompressedSize)) return std::nullopt;
    return inflated;
}

} // namespace atk::review
