#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace atk::review {

/// A class or staff list: a header row and the people, one row each, as
/// plain text cells (all rows padded to the same width).
struct Roster {
    QStringList headers;
    QVector<QStringList> rows;
};

struct RosterResult {
    bool ok = false;
    QString error;
    Roster roster;
};

/// Reads a roster from a .csv/.txt (comma, semicolon or tab separated;
/// UTF-8, UTF-16 or Windows ANSI as Excel writes it) or an .xlsx workbook
/// (its first sheet). With `firstRowIsHeader` false, columns are named
/// "Column A", "Column B"... Blank rows are dropped and cells trimmed.
RosterResult readRosterFile(const QString& path, bool firstRowIsHeader = true);
RosterResult parseCsv(const QByteArray& bytes, bool firstRowIsHeader = true);
RosterResult parseXlsx(const QByteArray& bytes, bool firstRowIsHeader = true);

/// Text from bytes: a BOM wins; else UTF-8 when valid; else Windows-1252.
QString decodeText(const QByteArray& bytes);

/// "A" -> 0, "Z" -> 25, "AA" -> 26 (spreadsheet column letters).
int columnIndex(const QString& letters);
/// 0 -> "A", 26 -> "AA".
QString columnLetters(int index);

} // namespace atk::review
