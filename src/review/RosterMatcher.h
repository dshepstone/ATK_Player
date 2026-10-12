#pragma once

#include "review/Roster.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

namespace atk::review {

/// Which roster columns mean what. -1 = not in the list.
struct RosterColumns {
    int id = -1;    ///< student or staff number, matched exactly
    int first = -1;
    int last = -1;
    int full = -1;  ///< a single "Name" / "Last, First" column
};

/// Guesses the columns from their headers ("Student ID", "First Name",
/// "Surname", "Name"...). Unrecognised columns stay -1.
RosterColumns guessColumns(const QStringList& headers);

/// A person's first and last name from a row, using `columns`. With only a
/// full-name column, "Last, First" and "First Last" are both understood.
struct PersonName {
    QString first;
    QString last;
    QString full() const;
};
PersonName personName(const QStringList& row, const RosterColumns& columns);

/// How sure a match is.
enum class MatchConfidence { None, Low, Medium, High };

struct RosterMatch {
    int roster = -1; ///< roster row, or -1 for no match
    int score = 0;   ///< 0-100
    MatchConfidence confidence = MatchConfidence::None;
};

/// Lower-case words of a name with accents removed: "José_Smith-Jones v2"
/// -> {"jose", "smith", "jones", "v2"}.
QStringList nameTokens(const QString& text);

/// Matches clip names (file names, without folders) to roster rows,
/// one-to-one, best first:
///   - the person's ID in the name: High (100)
///   - first and last name both in it, any order and separator: High (90)
///   - initial plus last name ("JSmith", "smith_j"): High (80)
///   - the last name alone, when no one else shares it: Medium (60)
///   - a last name one letter off ("Smtih"): Low (40), a suggestion only
/// The result has one entry per clip.
QVector<RosterMatch> matchClipsToRoster(const QStringList& clipNames, const Roster& roster,
                                        const RosterColumns& columns);

/// Everything a new-name template can use for one clip.
struct NameContext {
    int order = 0;           ///< 1-based position in the review
    QString id;
    QString first;
    QString last;
    QStringList headers;     ///< roster headers, for {col:Header}
    QStringList row;         ///< the person's roster row (may be empty)
    QString original;        ///< the clip's current name, without extension
    QString review;
    QDate date;
};

/// Fills a template such as "{order:02}_{last}_{first}". Tokens:
/// {order} {order:NN} {id} {first} {last} {full} {col:Header} {original}
/// {review} {date} (yyyy-MM-dd). Unknown tokens are kept as typed. The
/// result is made safe as a file name on Windows, macOS and Linux: illegal
/// characters become "_", and the ends are trimmed of spaces and dots.
QString renderName(const QString& nameTemplate, const NameContext& context);

/// Makes `name` usable as a file name everywhere (see renderName).
QString safeFileName(QString name);

} // namespace atk::review
