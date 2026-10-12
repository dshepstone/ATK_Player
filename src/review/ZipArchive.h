#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <optional>

namespace atk::review {

/// Decompresses a raw DEFLATE stream (RFC 1951): stored, fixed-Huffman and
/// dynamic-Huffman blocks. Small and self-contained, so reading an Excel file
/// adds no library to ATK Player. Empty on malformed input, or if the output
/// would exceed `maxOutput` bytes (a guard against zip bombs).
std::optional<QByteArray> inflateRaw(const QByteArray& deflated, qsizetype maxOutput = 64 * 1024 * 1024);

/// Read-only access to the files in a .zip archive held in memory: enough
/// for the .xlsx format (stored and deflated entries; no encryption, no
/// multi-disk, no ZIP64).
class ZipArchive {
public:
    /// False when `data` is not a readable zip archive.
    bool open(QByteArray data);

    bool contains(const QString& name) const { return m_entries.contains(name); }
    /// The uncompressed contents of `name`, or nothing when absent or damaged.
    std::optional<QByteArray> read(const QString& name) const;

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
};

} // namespace atk::review
