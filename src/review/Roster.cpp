#include "review/Roster.h"

#include "review/ZipArchive.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>
#include <QXmlStreamReader>

#include <algorithm>
#include <array>
#include <cmath>

namespace atk::review {
namespace {

RosterResult failure(const QString& message)
{
    RosterResult result;
    result.error = message;
    return result;
}

/// Trims cells, drops blank rows, pads rows to one width and names the columns.
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
    qsizetype width = 0;
    for (const QStringList& row : table) width = std::max(width, row.size());
    for (QStringList& row : table)
        while (row.size() < width) row.append(QString());
    // Trailing columns that are empty in every row are dropped.
    while (width > 0 && std::all_of(table.cbegin(), table.cend(), [&](const QStringList& row) {
               return row[width - 1].isEmpty();
           })) {
        --width;
        for (QStringList& row : table) row.removeLast();
    }
    RosterResult result;
    result.ok = true;
    if (firstRowIsHeader) {
        result.roster.headers = table.takeFirst();
        for (int i = 0; i < result.roster.headers.size(); ++i)
            if (result.roster.headers[i].isEmpty()) result.roster.headers[i] = QStringLiteral("Column %1").arg(columnLetters(i));
    } else {
        for (int i = 0; i < width; ++i) result.roster.headers << QStringLiteral("Column %1").arg(columnLetters(i));
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

RosterResult readRosterFile(const QString& path, bool firstRowIsHeader)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failure(file.errorString());
    if (file.size() > 32 * 1024 * 1024) return failure(QStringLiteral("The file is too large to be a class list."));
    const QByteArray bytes = file.readAll();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("xlsm")) return parseXlsx(bytes, firstRowIsHeader);
    if (suffix == QLatin1String("xls"))
        return failure(QStringLiteral("Old Excel (.xls) files cannot be read. In Excel, use File > Save As and "
                                      "choose Excel Workbook (.xlsx) or CSV."));
    return parseCsv(bytes, firstRowIsHeader);
}

} // namespace atk::review
