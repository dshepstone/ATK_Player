#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace atk::review {

/// Read-only access to the streams of an OLE2 compound file (the container of
/// Excel 97-2003 .xls workbooks), held in memory. Handles 512- and
/// 4096-byte sectors, the mini stream and extra DIFAT sectors; every chain is
/// bounds- and loop-checked, so a damaged file fails instead of hanging.
class CompoundFile {
public:
    /// True when `data` starts with the compound file signature.
    static bool hasSignature(const QByteArray& data);

    /// False when `data` is not a readable compound file.
    bool open(QByteArray data);

    /// The contents of the first stream named `name` (compared without
    /// case, as the format requires), or nothing when absent or damaged.
    std::optional<QByteArray> read(const QString& name) const;

private:
    struct Entry {
        QString name;
        quint8 type = 0;
        quint32 start = 0;
        quint32 size = 0;
    };
    std::optional<QByteArray> chain(const QVector<quint32>& table, quint32 start, quint32 size, bool mini) const;
    const char* sector(quint32 id) const;

    QByteArray m_data;
    quint32 m_sectorSize = 512;
    quint32 m_miniCutoff = 4096;
    QVector<quint32> m_fat;
    QVector<quint32> m_miniFat;
    QByteArray m_miniStream;
    QVector<Entry> m_entries;
};

} // namespace atk::review
