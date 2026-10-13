#include "review/CompoundFile.h"

#include <QtEndian>

namespace atk::review {
namespace {

constexpr quint32 kFree = 0xFFFFFFFF;       // FREESECT
constexpr quint32 kEndOfChain = 0xFFFFFFFE; // ENDOFCHAIN
constexpr quint32 kMaxRegular = 0xFFFFFFFA; // above this a sector id is special
constexpr char kSignature[] = "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1";

quint16 u16(const char* p) { return qFromLittleEndian<quint16>(p); }
quint32 u32(const char* p) { return qFromLittleEndian<quint32>(p); }

} // namespace

bool CompoundFile::hasSignature(const QByteArray& data)
{
    return data.startsWith(QByteArray::fromRawData(kSignature, 8));
}

const char* CompoundFile::sector(quint32 id) const
{
    if (id >= kMaxRegular) return nullptr;
    const qint64 offset = (qint64(id) + 1) * m_sectorSize;
    if (offset + m_sectorSize > m_data.size()) return nullptr;
    return m_data.constData() + offset;
}

std::optional<QByteArray> CompoundFile::chain(const QVector<quint32>& table, quint32 start, quint32 size, bool mini) const
{
    const quint32 unit = mini ? 64 : m_sectorSize;
    QByteArray out;
    out.reserve(qsizetype(size));
    quint32 id = start;
    // A chain can never be longer than the table: anything longer loops.
    for (qsizetype steps = 0; out.size() < qsizetype(size); ++steps) {
        if (id >= quint32(table.size()) || steps > table.size()) return std::nullopt;
        if (mini) {
            const qint64 offset = qint64(id) * unit;
            if (offset + unit > m_miniStream.size()) return std::nullopt;
            out.append(m_miniStream.constData() + offset, unit);
        } else {
            const char* p = sector(id);
            if (!p) return std::nullopt;
            out.append(p, unit);
        }
        id = table[id];
    }
    out.truncate(qsizetype(size));
    return out;
}

bool CompoundFile::open(QByteArray data)
{
    m_data = std::move(data);
    m_fat.clear();
    m_miniFat.clear();
    m_miniStream.clear();
    m_entries.clear();
    if (m_data.size() < 512 || !hasSignature(m_data)) return false;
    const char* h = m_data.constData();
    if (u16(h + 0x1C) != 0xFFFE) return false; // byte order mark: always little-endian
    const quint16 shift = u16(h + 0x1E);
    if (shift != 9 && shift != 12) return false;
    if (u16(h + 0x20) != 6) return false; // 64-byte mini sectors
    m_sectorSize = 1u << shift;
    const quint32 fatCount = u32(h + 0x2C);
    const quint32 firstDir = u32(h + 0x30);
    m_miniCutoff = u32(h + 0x38);
    const quint32 firstMiniFat = u32(h + 0x3C);
    const quint32 miniFatCount = u32(h + 0x40);
    quint32 difatSector = u32(h + 0x44);
    const quint32 difatCount = u32(h + 0x48);

    const qint64 sectorsInFile = m_data.size() / m_sectorSize;
    if (fatCount > sectorsInFile || difatCount > sectorsInFile || miniFatCount > sectorsInFile) return false;

    // The FAT's own sectors: 109 listed in the header, the rest in DIFAT sectors.
    QVector<quint32> fatSectors;
    for (int i = 0; i < 109 && quint32(fatSectors.size()) < fatCount; ++i) fatSectors.append(u32(h + 0x4C + i * 4));
    const quint32 perDifat = m_sectorSize / 4 - 1;
    for (quint32 n = 0; n < difatCount && quint32(fatSectors.size()) < fatCount; ++n) {
        const char* p = sector(difatSector);
        if (!p) return false;
        for (quint32 i = 0; i < perDifat && quint32(fatSectors.size()) < fatCount; ++i) fatSectors.append(u32(p + i * 4));
        difatSector = u32(p + perDifat * 4);
    }
    if (quint32(fatSectors.size()) != fatCount) return false;
    for (const quint32 id : fatSectors) {
        const char* p = sector(id);
        if (!p) return false;
        for (quint32 i = 0; i < m_sectorSize / 4; ++i) m_fat.append(u32(p + i * 4));
    }

    // Directory: 128-byte entries along its chain.
    QByteArray directory;
    quint32 id = firstDir;
    for (qsizetype steps = 0; id != kEndOfChain; ++steps) {
        if (id >= quint32(m_fat.size()) || steps > m_fat.size()) return false;
        const char* p = sector(id);
        if (!p) return false;
        directory.append(p, m_sectorSize);
        id = m_fat[id];
    }
    for (qsizetype offset = 0; offset + 128 <= directory.size(); offset += 128) {
        const char* e = directory.constData() + offset;
        Entry entry;
        const quint16 nameBytes = u16(e + 64);
        if (nameBytes >= 2 && nameBytes <= 64)
            entry.name = QString::fromUtf16(reinterpret_cast<const char16_t*>(e), nameBytes / 2 - 1);
        entry.type = quint8(e[66]);
        entry.start = u32(e + 116);
        entry.size = u32(e + 120); // version 4 files keep a high word that is zero for any sane size
        m_entries.append(entry);
    }
    if (m_entries.isEmpty() || m_entries.first().type != 5) return false; // the root entry

    // Small streams live in the mini stream (the root entry's data).
    id = firstMiniFat;
    for (quint32 n = 0; n < miniFatCount && id != kEndOfChain && id != kFree; ++n) {
        const char* p = sector(id);
        if (!p || id >= quint32(m_fat.size())) return false;
        for (quint32 i = 0; i < m_sectorSize / 4; ++i) m_miniFat.append(u32(p + i * 4));
        id = m_fat[id];
    }
    const Entry& root = m_entries.first();
    if (root.size > 0 && root.start < kMaxRegular) {
        if (qint64(root.size) > m_data.size()) return false;
        const auto mini = chain(m_fat, root.start, root.size, false);
        if (!mini) return false;
        m_miniStream = *mini;
    }
    return true;
}

std::optional<QByteArray> CompoundFile::read(const QString& name) const
{
    for (const Entry& entry : m_entries) {
        if (entry.type != 2 || entry.name.compare(name, Qt::CaseInsensitive) != 0) continue;
        if (entry.size == 0) return QByteArray();
        if (qint64(entry.size) > m_data.size()) return std::nullopt;
        if (entry.size < m_miniCutoff) return chain(m_miniFat, entry.start, entry.size, true);
        return chain(m_fat, entry.start, entry.size, false);
    }
    return std::nullopt;
}

} // namespace atk::review
