#include "review/RosterMatcher.h"

#include <QRegularExpression>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cstdlib>
#include <tuple>

namespace atk::review {
namespace {

int findHeader(const QStringList& headers, std::initializer_list<const char*> wanted)
{
    for (const char* w : wanted) {
        const QString target = QString::fromLatin1(w);
        for (int i = 0; i < headers.size(); ++i) {
            QString h = headers[i].toLower();
            h.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
            if (h == target) return i;
        }
    }
    return -1;
}

int editDistance(const QString& a, const QString& b)
{
    QVector<int> previous(b.size() + 1), current(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) previous[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0)});
        std::swap(previous, current);
    }
    return previous[b.size()];
}

MatchConfidence confidenceFor(int score)
{
    if (score >= 80) return MatchConfidence::High;
    if (score >= 60) return MatchConfidence::Medium;
    if (score > 0) return MatchConfidence::Low;
    return MatchConfidence::None;
}

} // namespace

QString PersonName::full() const
{
    return QStringList{first, last}.join(QLatin1Char(' ')).trimmed();
}

RosterColumns guessColumns(const QStringList& headers)
{
    RosterColumns c;
    c.id = findHeader(headers, {"studentid", "studentnumber", "studentno", "id", "number", "employeeid",
                                "staffid", "username", "userid", "login", "email"});
    c.first = findHeader(headers, {"firstname", "first", "givenname", "given", "forename", "preferredname"});
    c.last = findHeader(headers, {"lastname", "last", "surname", "familyname", "family"});
    if (c.first < 0 || c.last < 0) c.full = findHeader(headers, {"name", "fullname", "studentname", "student"});
    return c;
}

PersonName personName(const QStringList& row, const RosterColumns& columns)
{
    auto cell = [&](int index) { return index >= 0 && index < row.size() ? row[index].trimmed() : QString(); };
    PersonName name{cell(columns.first), cell(columns.last)};
    if ((name.first.isEmpty() || name.last.isEmpty()) && columns.full >= 0) {
        const QString full = cell(columns.full);
        if (full.contains(QLatin1Char(','))) { // "Last, First"
            name.last = full.section(QLatin1Char(','), 0, 0).trimmed();
            name.first = full.section(QLatin1Char(','), 1).trimmed();
        } else { // "First Last"
            const qsizetype space = full.lastIndexOf(QLatin1Char(' '));
            name.first = space > 0 ? full.left(space).trimmed() : QString();
            name.last = space > 0 ? full.mid(space + 1).trimmed() : full;
        }
    }
    return name;
}

QStringList nameTokens(const QString& text)
{
    QString plain = text.normalized(QString::NormalizationForm_D);
    plain.remove(QRegularExpression(QStringLiteral("\\p{Mn}"))); // accents
    plain = plain.toLower();
    // Also split camelCase ("JordanLogel" -> "jordan", "logel"), keeping the
    // unsplit words as well.
    QString spaced = text.normalized(QString::NormalizationForm_D);
    spaced.remove(QRegularExpression(QStringLiteral("\\p{Mn}")));
    spaced.replace(QRegularExpression(QStringLiteral("([a-z])([A-Z])")), QStringLiteral("\\1 \\2"));
    QStringList tokens = spaced.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")),
                                                Qt::SkipEmptyParts);
    for (const QString& t : plain.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts))
        if (!tokens.contains(t)) tokens.append(t);
    return tokens;
}

QVector<RosterMatch> matchClipsToRoster(const QStringList& clipNames, const Roster& roster,
                                        const RosterColumns& columns)
{
    struct Person {
        QString id;
        QStringList first;
        QStringList last;
    };
    QVector<Person> people;
    QHash<QString, int> lastNameCount;
    for (const QStringList& row : roster.rows) {
        const PersonName name = personName(row, columns);
        Person p;
        p.id = columns.id >= 0 && columns.id < row.size() ? row[columns.id].trimmed().toLower() : QString();
        p.first = nameTokens(name.first);
        p.last = nameTokens(name.last);
        for (const QString& t : p.last) ++lastNameCount[t];
        people.append(p);
    }

    // Score every pair, then assign best-first, one clip per person.
    std::vector<std::tuple<int, int, int>> pairs; // score, clip, person
    for (int c = 0; c < clipNames.size(); ++c) {
        const QStringList tokens = nameTokens(clipNames[c]);
        const QSet<QString> tokenSet(tokens.cbegin(), tokens.cend());
        const QString joined = tokens.join(QString());
        for (int p = 0; p < people.size(); ++p) {
            const Person& person = people[p];
            int score = 0;
            if (person.id.size() >= 3
                && (tokenSet.contains(person.id) || (person.id.contains(QRegularExpression(QStringLiteral("\\d")))
                                                     && joined.contains(person.id))))
                score = 100;
            if (score == 0 && !person.last.isEmpty()) {
                const bool lastIn = std::all_of(person.last.cbegin(), person.last.cend(),
                                                [&](const QString& t) { return tokenSet.contains(t); });
                const bool firstIn = !person.first.isEmpty()
                    && std::all_of(person.first.cbegin(), person.first.cend(),
                                   [&](const QString& t) { return tokenSet.contains(t); });
                const QString last = person.last.join(QString());
                const QString initial = person.first.isEmpty() ? QString() : person.first.first().left(1);
                if (lastIn && firstIn) score = 90;
                else if (!initial.isEmpty() && (tokenSet.contains(initial + last) || tokenSet.contains(last + initial)
                                                || (lastIn && tokenSet.contains(initial))))
                    score = 80;
                else if (lastIn && std::all_of(person.last.cbegin(), person.last.cend(),
                                               [&](const QString& t) { return lastNameCount.value(t) == 1; }))
                    score = 60;
                else if (last.size() >= 5)
                    for (const QString& t : tokens)
                        if (std::abs(int(t.size()) - int(last.size())) <= 1 && editDistance(t, last) == 1) score = 40;
            }
            if (score > 0) pairs.emplace_back(score, c, p);
        }
    }
    std::stable_sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
    QVector<RosterMatch> result(clipNames.size());
    QVector<bool> personTaken(people.size(), false);
    for (const auto& [score, clip, person] : pairs) {
        if (result[clip].roster >= 0 || personTaken[person]) continue;
        result[clip] = {person, score, confidenceFor(score)};
        personTaken[person] = true;
    }
    return result;
}

QString safeFileName(QString name)
{
    static const QRegularExpression illegal(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    name.replace(illegal, QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) name.chop(1);
    // Names Windows reserves for devices, with or without an extension.
    static const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(name).hasMatch()) name += QLatin1Char('_');
    return name;
}

QString renderName(const QString& nameTemplate, const NameContext& context)
{
    static const QRegularExpression token(QStringLiteral(R"(\{([a-zA-Z]+)(?::([^}]*))?\})"));
    QString out;
    qsizetype last = 0;
    auto it = token.globalMatch(nameTemplate);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += nameTemplate.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const QString key = m.captured(1).toLower();
        const QString arg = m.captured(2);
        if (key == QLatin1String("order")) {
            const int width = arg.isEmpty() ? 0 : arg.toInt();
            out += QStringLiteral("%1").arg(context.order, width, 10, QLatin1Char('0'));
        } else if (key == QLatin1String("id")) {
            out += context.id;
        } else if (key == QLatin1String("first")) {
            out += context.first;
        } else if (key == QLatin1String("last")) {
            out += context.last;
        } else if (key == QLatin1String("full")) {
            out += QStringList{context.first, context.last}.join(QLatin1Char(' ')).trimmed();
        } else if (key == QLatin1String("col")) {
            const int index = int(context.headers.indexOf(arg));
            if (index >= 0 && index < context.row.size()) out += context.row[index];
        } else if (key == QLatin1String("original")) {
            out += context.original;
        } else if (key == QLatin1String("review")) {
            out += context.review;
        } else if (key == QLatin1String("date")) {
            out += context.date.toString(Qt::ISODate);
        } else {
            out += m.captured(0); // unknown: kept as typed, so a typo shows
        }
    }
    out += nameTemplate.mid(last);
    return safeFileName(out);
}

} // namespace atk::review
