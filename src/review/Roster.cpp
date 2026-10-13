#include "review/Roster.h"

#include "review/CompoundFile.h"
#include "review/RosterMatcher.h"
#include "review/ZipArchive.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

namespace atk::review {
namespace {

RosterResult failure(const QString& message)
{
    RosterResult result;
    result.error = message;
    return result;
}

/// Trims cells, drops blank rows and columns, and names the columns.
///
/// Lists exported from school and HR systems are often reports: a title,
/// course details, then the real header row, and totals or signature lines
/// after the people. With a header row, the first row (of the first 50)
/// whose headings name people ("Name", "First Name"...) is taken as the
/// header, and rows with neither a name nor an ID are left out.
RosterResult finish(QVector<QStringList> table, bool firstRowIsHeader)
{
    for (QStringList& row : table)
        for (QString& cell : row) cell = cell.trimmed();
    table.erase(std::remove_if(table.begin(), table.end(),
                               [](const QStringList& row) {
                                   return std::all_of(row.cbegin(), row.cend(),
                                                      [](const QString& c) { return c.isEmpty(); });
                               }),
                table.end());
    if (table.isEmpty()) return failure(QStringLiteral("The list is empty."));

    if (firstRowIsHeader) {
        const auto namesPeople = [](const RosterColumns& c) { return c.full >= 0 || (c.first >= 0 && c.last >= 0); };
        for (qsizetype i = 0; i < std::min<qsizetype>(table.size(), 50); ++i) {
            if (!namesPeople(guessColumns(table[i]))) continue;
            table.erase(table.begin(), table.begin() + i);
            const RosterColumns columns = guessColumns(table.first());
            const auto cell = [](const QStringList& row, int index) {
                return index >= 0 && index < row.size() ? row[index] : QString();
            };
            table.erase(std::remove_if(table.begin() + 1, table.end(),
                                       [&](const QStringList& row) {
                                           return cell(row, columns.id).isEmpty() && cell(row, columns.first).isEmpty()
                                               && cell(row, columns.last).isEmpty() && cell(row, columns.full).isEmpty();
                                       }),
                        table.end());
            break;
        }
    }

    // Columns empty in every row (merged-cell layouts leave many) are dropped;
    // unnamed ones keep their spreadsheet letter.
    qsizetype width = 0;
    for (const QStringList& row : table) width = std::max(width, row.size());
    QVector<int> kept;
    for (int c = 0; c < width; ++c)
        if (std::any_of(table.cbegin(), table.cend(), [&](const QStringList& row) { return c < row.size() && !row[c].isEmpty(); }))
            kept.append(c);
    for (QStringList& row : table) {
        QStringList compact;
        compact.reserve(kept.size());
        for (const int c : kept) compact.append(c < row.size() ? row[c] : QString());
        row = std::move(compact);
    }

    RosterResult result;
    result.ok = true;
    if (firstRowIsHeader) {
        result.roster.headers = table.takeFirst();
        for (int i = 0; i < result.roster.headers.size(); ++i)
            if (result.roster.headers[i].isEmpty())
                result.roster.headers[i] = QStringLiteral("Column %1").arg(columnLetters(kept[i]));
    } else {
        for (const int c : kept) result.roster.headers << QStringLiteral("Column %1").arg(columnLetters(c));
    }
    result.roster.rows = std::move(table);
    return result;
}

QChar detectDelimiter(const QString& text)
{
    // Count candidates on the first line, outside quotes.
    int comma = 0, semicolon = 0, tab = 0;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) quoted = !quoted;
        else if (!quoted && (c == QLatin1Char('\n') || c == QLatin1Char('\r'))) break;
        else if (!quoted && c == QLatin1Char(',')) ++comma;
        else if (!quoted && c == QLatin1Char(';')) ++semicolon;
        else if (!quoted && c == QLatin1Char('\t')) ++tab;
    }
    if (tab >= comma && tab >= semicolon && tab > 0) return QLatin1Char('\t');
    if (semicolon > comma) return QLatin1Char(';');
    return QLatin1Char(',');
}

} // namespace

QString decodeText(const QByteArray& bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF")) return QString::fromUtf8(bytes.mid(3));
    if (bytes.startsWith("\xFF\xFE")) {
        QStringDecoder decoder(QStringConverter::Utf16LE);
        return decoder(bytes.mid(2));
    }
    if (bytes.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(QStringConverter::Utf16BE);
        return decoder(bytes.mid(2));
    }
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString text = utf8(bytes);
    if (!utf8.hasError()) return text;
    // Windows-1252: Latin-1 plus typographic characters in 0x80-0x9F, which
    // is what Excel's "CSV (Comma delimited)" writes on Western Windows.
    static constexpr std::array<char16_t, 32> k80{
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    QString result;
    result.reserve(bytes.size());
    for (const char byte : bytes) {
        const uchar b = uchar(byte);
        result.append(b >= 0x80 && b < 0xA0 ? QChar(k80[b - 0x80]) : QChar(b));
    }
    return result;
}

int columnIndex(const QString& letters)
{
    int index = 0;
    for (const QChar c : letters) {
        if (!c.isLetter()) break;
        index = index * 26 + (c.toUpper().unicode() - 'A' + 1);
    }
    return index - 1;
}

QString columnLetters(int index)
{
    QString letters;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) letters.prepend(QChar('A' + (n - 1) % 26));
    return letters;
}

RosterResult parseCsv(const QByteArray& bytes, bool firstRowIsHeader)
{
    const QString text = decodeText(bytes);
    const QChar delimiter = detectDelimiter(text);
    QVector<QStringList> table;
    QStringList row;
    QString cell;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text[i + 1] == QLatin1Char('"')) {
                    cell.append(QLatin1Char('"')); // "" inside quotes
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell.append(c);
            }
        } else if (c == QLatin1Char('"') && cell.trimmed().isEmpty()) {
            quoted = true;
            cell.clear();
        } else if (c == delimiter) {
            row.append(cell);
            cell.clear();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) ++i;
            row.append(cell);
            cell.clear();
            table.append(row);
            row.clear();
        } else {
            cell.append(c);
        }
    }
    if (!cell.isEmpty() || !row.isEmpty()) {
        row.append(cell);
        table.append(row);
    }
    return finish(std::move(table), firstRowIsHeader);
}

RosterResult parseXlsx(const QByteArray& bytes, bool firstRowIsHeader)
{
    ZipArchive zip;
    if (!zip.open(bytes)) return failure(QStringLiteral("This is not a readable Excel (.xlsx) file."));

    // The first sheet, through the workbook's relationships.
    QString sheetPath = QStringLiteral("xl/worksheets/sheet1.xml");
    if (const auto workbook = zip.read(QStringLiteral("xl/workbook.xml"))) {
        QString firstId;
        QXmlStreamReader xml(*workbook);
        while (!xml.atEnd() && firstId.isEmpty())
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("sheet"))
                for (const QXmlStreamAttribute& a : xml.attributes())
                    if (a.name() == QLatin1String("id")) firstId = a.value().toString();
        if (const auto rels = zip.read(QStringLiteral("xl/_rels/workbook.xml.rels")); rels && !firstId.isEmpty()) {
            QXmlStreamReader r(*rels);
            while (!r.atEnd()) {
                if (r.readNext() != QXmlStreamReader::StartElement || r.name() != QLatin1String("Relationship")) continue;
                if (r.attributes().value(QLatin1String("Id")) != firstId) continue;
                QString target = r.attributes().value(QLatin1String("Target")).toString();
                sheetPath = target.startsWith(QLatin1Char('/')) ? target.mid(1) : QStringLiteral("xl/") + target;
                break;
            }
        }
    }

    QStringList shared;
    if (const auto strings = zip.read(QStringLiteral("xl/sharedStrings.xml"))) {
        QXmlStreamReader xml(*strings);
        QString current;
        bool inItem = false;
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement && xml.name() == QLatin1String("si")) {
                inItem = true;
                current.clear();
            } else if (token == QXmlStreamReader::StartElement && inItem && xml.name() == QLatin1String("t")) {
                current += xml.readElementText(); // rich text: several runs
            } else if (token == QXmlStreamReader::StartElement && xml.name() == QLatin1String("rPh")) {
                xml.skipCurrentElement(); // phonetic guides are not part of the text
            } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("si")) {
                shared.append(current);
                inItem = false;
            }
        }
    }

    const auto sheet = zip.read(sheetPath);
    if (!sheet) return failure(QStringLiteral("The workbook has no readable first sheet."));
    QVector<QStringList> table;
    QXmlStreamReader xml(*sheet);
    QStringList row;
    int column = -1;
    QString type;
    QString value;
    bool inCell = false;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("row")) {
                row.clear();
            } else if (xml.name() == QLatin1String("c")) {
                inCell = true;
                const QString ref = xml.attributes().value(QLatin1String("r")).toString();
                column = ref.isEmpty() ? int(row.size()) : columnIndex(ref);
                type = xml.attributes().value(QLatin1String("t")).toString();
                value.clear();
            } else if (inCell && (xml.name() == QLatin1String("v") || xml.name() == QLatin1String("t"))) {
                value += xml.readElementText();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("c")) {
                inCell = false;
                QString text = value;
                if (type == QLatin1String("s")) {
                    bool ok = false;
                    const int index = value.toInt(&ok);
                    text = ok && index >= 0 && index < shared.size() ? shared[index] : QString();
                } else if (type == QLatin1String("b")) {
                    text = value == QLatin1String("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
                } else if (type.isEmpty() || type == QLatin1String("n")) {
                    // Whole numbers such as student IDs read without ".0".
                    bool ok = false;
                    const double number = value.toDouble(&ok);
                    if (ok && number == double(qint64(number)) && std::abs(number) < 1e15)
                        text = QString::number(qint64(number));
                }
                if (column >= 0 && column < 16384) {
                    while (row.size() <= column) row.append(QString());
                    row[column] = text;
                }
            } else if (xml.name() == QLatin1String("row")) {
                table.append(row);
            }
        }
    }
    if (xml.hasError()) return failure(QStringLiteral("The sheet could not be read: %1").arg(xml.errorString()));
    return finish(std::move(table), firstRowIsHeader);
}

namespace {

/// The records of a BIFF stream: a 2-byte type, a 2-byte length, the data.
struct BiffRecord {
    quint16 type = 0;
    QByteArray data;
};

QVector<BiffRecord> biffRecords(const QByteArray& stream, qsizetype from)
{
    QVector<BiffRecord> records;
    qsizetype at = from;
    while (at + 4 <= stream.size()) {
        const quint16 type = qFromLittleEndian<quint16>(stream.constData() + at);
        const quint16 length = qFromLittleEndian<quint16>(stream.constData() + at + 2);
        if (at + 4 + length > stream.size()) break;
        records.append({type, stream.mid(at + 4, length)});
        at += 4 + length;
        if (type == 0x000A) break; // EOF of this substream
    }
    return records;
}

/// Reads text that may run on into CONTINUE records. Where characters cross
/// into the next record, it starts with a fresh "compressed or UTF-16" byte.
class BiffText {
public:
    explicit BiffText(QVector<QByteArray> parts) : m_parts(std::move(parts)) {}

    bool atEnd() { return !settle(); }
    bool skip(qsizetype n)
    {
        while (n > 0) {
            if (!settle()) return false;
            const qsizetype take = std::min(n, m_parts[m_part].size() - m_pos);
            m_pos += take;
            n -= take;
        }
        return true;
    }
    std::optional<quint32> number(int bytes)
    {
        quint32 value = 0;
        for (int i = 0; i < bytes; ++i) {
            if (!settle()) return std::nullopt;
            value |= quint32(quint8(m_parts[m_part][m_pos++])) << (8 * i);
        }
        return value;
    }
    /// An XLUnicodeRichExtendedString (SST) or, with `rich` false, an
    /// XLUnicodeString (LABEL, STRING).
    std::optional<QString> string(bool rich)
    {
        const auto count = number(2);
        const auto flags = number(1);
        if (!count || !flags) return std::nullopt;
        quint32 runs = 0;
        quint32 extra = 0;
        if (rich && (*flags & 0x08)) {
            const auto r = number(2);
            if (!r) return std::nullopt;
            runs = *r;
        }
        if (rich && (*flags & 0x04)) {
            const auto e = number(4);
            if (!e) return std::nullopt;
            extra = *e;
        }
        bool wide = *flags & 0x01;
        QString text;
        text.reserve(qsizetype(*count));
        quint32 left = *count;
        while (left > 0) {
            if (m_pos >= m_parts.value(m_part).size()) {
                // The characters go on in the next CONTINUE record.
                if (++m_part >= m_parts.size()) return std::nullopt;
                m_pos = 0;
                if (m_parts[m_part].isEmpty()) return std::nullopt;
                wide = quint8(m_parts[m_part][m_pos++]) & 0x01;
                continue;
            }
            const QByteArray& part = m_parts[m_part];
            const qsizetype width = wide ? 2 : 1;
            const qsizetype fit = std::min<qsizetype>(left, (part.size() - m_pos) / width);
            if (fit == 0) return std::nullopt; // half a character
            const char* p = part.constData() + m_pos;
            if (wide) {
                for (qsizetype i = 0; i < fit; ++i) text.append(QChar(qFromLittleEndian<quint16>(p + 2 * i)));
            } else {
                text.append(QString::fromLatin1(p, fit)); // "compressed": the high bytes are zero
            }
            m_pos += fit * width;
            left -= quint32(fit);
        }
        if (!skip(qsizetype(runs) * 4 + extra)) return std::nullopt;
        return text;
    }

private:
    /// Moves past finished parts; false at the very end.
    bool settle()
    {
        while (m_part < m_parts.size() && m_pos >= m_parts[m_part].size()) {
            ++m_part;
            m_pos = 0;
        }
        return m_part < m_parts.size();
    }

    QVector<QByteArray> m_parts;
    qsizetype m_part = 0;
    qsizetype m_pos = 0;
};

QString numberText(double number)
{
    // Whole numbers such as student IDs read without ".0".
    if (std::isfinite(number) && number == double(qint64(number)) && std::abs(number) < 1e15)
        return QString::number(qint64(number));
    return QString::number(number, 'g', 15);
}

double rkNumber(quint32 rk)
{
    double value = 0;
    if (rk & 0x02) {
        value = double(qint32(rk) >> 2);
    } else {
        const quint64 bits = quint64(rk & 0xFFFFFFFC) << 32;
        std::memcpy(&value, &bits, sizeof value);
    }
    return (rk & 0x01) ? value / 100 : value;
}

} // namespace

RosterResult parseXls(const QByteArray& bytes, bool firstRowIsHeader)
{
    CompoundFile file;
    if (!file.open(bytes)) return failure(QStringLiteral("This is not a readable Excel (.xls) file."));
    auto stream = file.read(QStringLiteral("Workbook"));
    if (!stream) stream = file.read(QStringLiteral("Book")); // Excel 5/95
    if (!stream) return failure(QStringLiteral("This file has no Excel workbook in it."));

    // The workbook globals: the shared strings and where each sheet starts.
    const QVector<BiffRecord> globals = biffRecords(*stream, 0);
    if (globals.isEmpty() || globals.first().type != 0x0809 || globals.first().data.size() < 2)
        return failure(QStringLiteral("This is not a readable Excel (.xls) file."));
    if (qFromLittleEndian<quint16>(globals.first().data.constData()) != 0x0600)
        return failure(QStringLiteral("This workbook is from Excel 95 or earlier. In Excel, use File > Save As and "
                                      "choose Excel Workbook (.xlsx)."));
    QStringList shared;
    qint64 sheetAt = -1;
    qint64 hiddenSheetAt = -1;
    for (qsizetype i = 0; i < globals.size(); ++i) {
        const BiffRecord& record = globals[i];
        if (record.type == 0x002F)
            return failure(QStringLiteral("This workbook is password protected. Remove the password in Excel and "
                                          "save it again."));
        if (record.type == 0x0085 && record.data.size() >= 6) { // BOUNDSHEET
            const qint64 at = qFromLittleEndian<quint32>(record.data.constData());
            const bool worksheet = quint8(record.data[5]) == 0;
            const bool visible = (quint8(record.data[4]) & 0x03) == 0;
            if (worksheet && visible && sheetAt < 0) sheetAt = at;
            if (worksheet && hiddenSheetAt < 0) hiddenSheetAt = at;
        } else if (record.type == 0x00FC && record.data.size() >= 8) { // SST, then its CONTINUEs
            QVector<QByteArray> parts{record.data.mid(8)};
            while (i + 1 < globals.size() && globals[i + 1].type == 0x003C) parts.append(globals[++i].data);
            const quint32 unique = qFromLittleEndian<quint32>(record.data.constData() + 4);
            BiffText text(std::move(parts));
            for (quint32 n = 0; n < unique && n < 4'000'000 && !text.atEnd(); ++n) {
                const auto s = text.string(true);
                if (!s) break; // keep what was read; damaged tails show as blank cells
                shared.append(*s);
            }
        }
    }
    if (sheetAt < 0) sheetAt = hiddenSheetAt;
    if (sheetAt < 0 || sheetAt >= stream->size()) return failure(QStringLiteral("The workbook has no readable first sheet."));

    const QVector<BiffRecord> sheet = biffRecords(*stream, sheetAt);
    if (sheet.isEmpty() || sheet.first().type != 0x0809)
        return failure(QStringLiteral("The workbook has no readable first sheet."));
    QVector<QStringList> table;
    const auto put = [&](quint16 row, quint16 column, const QString& text) {
        if (column >= 256) return;
        while (table.size() <= row) table.append(QStringList());
        QStringList& cells = table[row];
        while (cells.size() <= column) cells.append(QString());
        cells[column] = text;
    };
    const auto u16At = [](const QByteArray& d, int at) { return qFromLittleEndian<quint16>(d.constData() + at); };
    const auto u32At = [](const QByteArray& d, int at) { return qFromLittleEndian<quint32>(d.constData() + at); };
    int pendingRow = -1; // a formula whose text follows in a STRING record
    int pendingColumn = -1;
    for (qsizetype i = 1; i < sheet.size(); ++i) {
        const BiffRecord& record = sheet[i];
        const QByteArray& d = record.data;
        switch (record.type) {
        case 0x00FD: // LABELSST
            if (d.size() >= 10) {
                const quint32 index = u32At(d, 6);
                put(u16At(d, 0), u16At(d, 2), index < quint32(shared.size()) ? shared[index] : QString());
            }
            break;
        case 0x0204: // LABEL
        case 0x00D6: // RSTRING
            if (d.size() >= 9) {
                BiffText text({d.mid(6)});
                if (const auto s = text.string(false)) put(u16At(d, 0), u16At(d, 2), *s);
            }
            break;
        case 0x0203: // NUMBER
            if (d.size() >= 14) {
                put(u16At(d, 0), u16At(d, 2), numberText(qFromLittleEndian<double>(d.constData() + 6)));
            }
            break;
        case 0x027E: // RK
            if (d.size() >= 10) put(u16At(d, 0), u16At(d, 2), numberText(rkNumber(u32At(d, 6))));
            break;
        case 0x00BD: // MULRK: several RK cells in one row
            if (d.size() >= 6) {
                const quint16 row = u16At(d, 0);
                const quint16 first = u16At(d, 2);
                const int count = int(d.size() - 6) / 6;
                for (int n = 0; n < count; ++n) put(row, quint16(first + n), numberText(rkNumber(u32At(d, 4 + n * 6 + 2))));
            }
            break;
        case 0x0205: // BOOLERR
            if (d.size() >= 8 && d[7] == 0)
                put(u16At(d, 0), u16At(d, 2), d[6] ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
            break;
        case 0x0006: // FORMULA: its last calculated value
            if (d.size() >= 14) {
                const quint16 row = u16At(d, 0);
                const quint16 column = u16At(d, 2);
                if (u16At(d, 12) == 0xFFFF) {
                    const quint8 kind = quint8(d[6]);
                    if (kind == 0) {
                        pendingRow = row;
                        pendingColumn = column;
                    } else if (kind == 1) {
                        put(row, column, d[8] ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
                    }
                } else {
                    put(row, column, numberText(qFromLittleEndian<double>(d.constData() + 6)));
                }
            }
            break;
        case 0x0207: // STRING: the text result of the formula before it
            if (pendingRow >= 0) {
                QVector<QByteArray> parts{d};
                while (i + 1 < sheet.size() && sheet[i + 1].type == 0x003C) parts.append(sheet[++i].data);
                BiffText text(std::move(parts));
                if (const auto s = text.string(false)) put(quint16(pendingRow), quint16(pendingColumn), *s);
                pendingRow = pendingColumn = -1;
            }
            break;
        default:
            break;
        }
    }
    return finish(std::move(table), firstRowIsHeader);
}

RosterResult readRosterFile(const QString& path, bool firstRowIsHeader)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(file.errorString());
    if (file.size() > 32 * 1024 * 1024) return failure(QStringLiteral("The file is too large to be a team list."));
    const QByteArray bytes = file.readAll();
    // The contents decide: school and HR systems often save an .xlsx or a
    // CSV under an .xls name.
    if (CompoundFile::hasSignature(bytes)) return parseXls(bytes, firstRowIsHeader);
    if (bytes.startsWith("PK\x03\x04")) return parseXlsx(bytes, firstRowIsHeader);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("xlsm") || suffix == QLatin1String("xls")) {
        const QByteArray start = bytes.left(512).trimmed().toLower();
        if (start.startsWith("<") || start.startsWith("\xef\xbb\xbf<"))
            return failure(QStringLiteral("This file is a web page saved with an Excel name. Open it in Excel and use "
                                          "File > Save As to save it as Excel Workbook (.xlsx) or CSV."));
        if (suffix != QLatin1String("xls")) return parseXlsx(bytes, firstRowIsHeader);
    }
    return parseCsv(bytes, firstRowIsHeader);
}

} // namespace atk::review
