#include "review/BatchRename.h"

#include "review/RosterMatcher.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QUuid>

#include <algorithm>

namespace atk::review {
namespace {

constexpr auto kJournalPrefix = ".atk-rename-";

struct Step {
    QString from;
    QString temp;
    QString to;
};

QString folded(const QString& path, bool caseInsensitive)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return caseInsensitive ? clean.toCaseFolded() : clean;
}

bool writeJournal(const QString& path, const QString& state, const QVector<Step>& steps, qint64 created)
{
    QJsonArray items;
    for (const Step& s : steps)
        items.append(QJsonObject{{QStringLiteral("from"), s.from}, {QStringLiteral("temp"), s.temp},
                                 {QStringLiteral("to"), s.to}});
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("ATKRenameJournal")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("state"), state},
                           {QStringLiteral("created"), double(created)},
                           {QStringLiteral("items"), items}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool readJournal(const QString& path, QString* state, QVector<Step>* steps, qint64* created)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("ATKRenameJournal")) return false;
    *state = root.value(QStringLiteral("state")).toString();
    *created = qint64(root.value(QStringLiteral("created")).toDouble());
    steps->clear();
    for (const QJsonValue& v : root.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        steps->append({o.value(QStringLiteral("from")).toString(), o.value(QStringLiteral("temp")).toString(),
                       o.value(QStringLiteral("to")).toString()});
    }
    return true;
}

QString temporaryName(const QString& from, int index)
{
    const QFileInfo info(from);
    const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    return info.dir().filePath(QStringLiteral(".atk-renaming-%1-%2%3")
                                   .arg(QUuid::createUuid().toString(QUuid::Id128).left(8))
                                   .arg(index)
                                   .arg(suffix));
}

/// Renames one file. Windows refuses to rename a file that is open, and a
/// file is often open for a moment after ATK Player lets go of it (a decoder
/// thread finishing its close) or while antivirus, the search indexer or
/// OneDrive looks at it, so a refusal is retried for up to about 3 seconds.
/// Elsewhere an open file can be renamed and a failure is final.
bool moveFile(const QString& from, const QString& to)
{
#ifdef Q_OS_WIN
    constexpr int kAttempts = 40;
#else
    constexpr int kAttempts = 1;
#endif
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (QFile::rename(from, to)) return true;
        if (!QFileInfo::exists(from) || QFileInfo::exists(to) || !QFileInfo(QFileInfo(to).absolutePath()).isDir())
            return false; // not a lock: final
        if (attempt + 1 < kAttempts) QThread::msleep(75);
    }
    return false;
}

/// Moves files from -> temp -> to; undoes what it did on any failure.
bool runSteps(const QVector<Step>& steps, QString* error)
{
    QVector<int> parked;
    for (int i = 0; i < steps.size(); ++i) {
        if (QFileInfo::exists(steps[i].temp) || !moveFile(steps[i].from, steps[i].temp)) {
            *error = QStringLiteral("Could not rename %1 (is it open in another program?).")
                         .arg(QDir::toNativeSeparators(steps[i].from));
            for (auto it = parked.crbegin(); it != parked.crend(); ++it) moveFile(steps[*it].temp, steps[*it].from);
            return false;
        }
        parked.append(i);
    }
    QVector<int> placed;
    for (int i = 0; i < steps.size(); ++i) {
        if (QFileInfo::exists(steps[i].to) || !moveFile(steps[i].temp, steps[i].to)) {
            *error = QStringLiteral("Could not rename to %1.").arg(QDir::toNativeSeparators(steps[i].to));
            for (auto it = placed.crbegin(); it != placed.crend(); ++it) moveFile(steps[*it].to, steps[*it].temp);
            for (auto it = parked.crbegin(); it != parked.crend(); ++it) moveFile(steps[*it].temp, steps[*it].from);
            return false;
        }
        placed.append(i);
    }
    return true;
}

QStringList journalsIn(const QString& folder)
{
    QDir dir(folder);
    QStringList paths;
    for (const QString& name : dir.entryList({QString::fromLatin1(kJournalPrefix) + QStringLiteral("*.json")},
                                             QDir::Files | QDir::Hidden, QDir::Name))
        paths << dir.filePath(name);
    return paths;
}

} // namespace

QString renameProblemText(RenameProblem problem)
{
    switch (problem) {
    case RenameProblem::None: return {};
    case RenameProblem::Unchanged: return QStringLiteral("Already has this name");
    case RenameProblem::SourceMissing: return QStringLiteral("The file is missing");
    case RenameProblem::EmptyName: return QStringLiteral("The new name is empty");
    case RenameProblem::IllegalName: return QStringLiteral("Not allowed as a file name on every system");
    case RenameProblem::PathTooLong: return QStringLiteral("The new name is too long");
    case RenameProblem::DuplicateTarget: return QStringLiteral("Another clip would get the same name");
    case RenameProblem::TargetExists: return QStringLiteral("A file with this name already exists");
    }
    return {};
}

bool RenamePlan::ok() const
{
    return std::all_of(items.cbegin(), items.cend(), [](const PlannedRename& r) {
        return r.problem == RenameProblem::None || r.problem == RenameProblem::Unchanged;
    });
}

int RenamePlan::changeCount() const
{
    return int(std::count_if(items.cbegin(), items.cend(),
                             [](const PlannedRename& r) { return r.problem == RenameProblem::None; }));
}

RenamePlan planRenames(const QVector<RenameItem>& items, bool caseInsensitive)
{
    RenamePlan plan;
    QSet<QString> sources;
    QHash<QString, int> targets;
    for (const RenameItem& item : items) sources.insert(folded(item.from, caseInsensitive));
    for (const RenameItem& item : items) ++targets[folded(item.to, caseInsensitive)];
    for (const RenameItem& item : items) {
        PlannedRename r{QFileInfo(item.from).absoluteFilePath(), QFileInfo(item.to).absoluteFilePath(), RenameProblem::None};
        const QString name = QFileInfo(item.to).fileName();
        const QString base = QFileInfo(item.to).completeBaseName();
        if (!QFileInfo(item.from).isFile()) r.problem = RenameProblem::SourceMissing;
        else if (QDir::cleanPath(r.from) == QDir::cleanPath(r.to)) r.problem = RenameProblem::Unchanged;
        else if (base.trimmed().isEmpty()) r.problem = RenameProblem::EmptyName;
        else if (safeFileName(base) != base || safeFileName(name) != name) r.problem = RenameProblem::IllegalName;
        else if (name.size() > 240 || r.to.size() > 259) r.problem = RenameProblem::PathTooLong;
        else if (targets.value(folded(item.to, caseInsensitive)) > 1) r.problem = RenameProblem::DuplicateTarget;
        else if (QFileInfo::exists(r.to) && !sources.contains(folded(item.to, caseInsensitive)))
            r.problem = RenameProblem::TargetExists;
        plan.items.append(r);
    }
    return plan;
}

RenameResult executeRenames(const RenamePlan& plan)
{
    RenameResult result;
    if (!plan.ok()) {
        result.error = QStringLiteral("The plan has problems; nothing was renamed.");
        return result;
    }
    QVector<Step> steps;
    for (const PlannedRename& r : plan.items)
        if (r.problem == RenameProblem::None) steps.append({r.from, temporaryName(r.from, int(steps.size())), r.to});
    if (steps.isEmpty()) {
        result.ok = true;
        return result;
    }
    const qint64 created = QDateTime::currentMSecsSinceEpoch();
    const QString folder = QFileInfo(steps.first().from).absolutePath();
    result.journalPath = QDir(folder).filePath(
        QString::fromLatin1(kJournalPrefix) + QDateTime::fromMSecsSinceEpoch(created).toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
        + QStringLiteral(".json"));
    // The journal comes first: if the power goes, the next launch can undo.
    if (!writeJournal(result.journalPath, QStringLiteral("started"), steps, created)) {
        result.error = QStringLiteral("Could not write the rename journal in %1; nothing was renamed.")
                           .arg(QDir::toNativeSeparators(folder));
        return result;
    }
    if (!runSteps(steps, &result.error)) {
        writeJournal(result.journalPath, QStringLiteral("rolledBack"), steps, created);
        return result;
    }
    writeJournal(result.journalPath, QStringLiteral("done"), steps, created);
    result.ok = true;
    result.renamed = int(steps.size());
    return result;
}

bool readRenameJournal(const QString& journalPath, QVector<RenameItem>* items, QString* state)
{
    QVector<Step> steps;
    qint64 created = 0;
    if (!readJournal(journalPath, state, &steps, &created)) return false;
    items->clear();
    for (const Step& s : steps) items->append({s.from, s.to});
    return true;
}

RenameResult undoRenames(const QString& journalPath)
{
    RenameResult result;
    result.journalPath = journalPath;
    QString state;
    QVector<Step> steps;
    qint64 created = 0;
    if (!readJournal(journalPath, &state, &steps, &created) || state != QLatin1String("done")) {
        result.error = QStringLiteral("There is no finished rename to undo.");
        return result;
    }
    // An original name may be in use only by another file of this same
    // rename (a swap, or a case-only change on Windows/macOS).
    QSet<QString> renamedTo;
    for (const Step& s : steps) renamedTo.insert(folded(s.to, true));
    QVector<Step> reverse;
    for (int i = 0; i < steps.size(); ++i) {
        const bool originalTaken = QFileInfo::exists(steps[i].from) && !renamedTo.contains(folded(steps[i].from, true));
        if (!QFileInfo::exists(steps[i].to) || originalTaken) {
            result.error = QStringLiteral("The files have changed since the rename (%1), so it cannot be undone safely.")
                               .arg(QDir::toNativeSeparators(QFileInfo(steps[i].to).fileName()));
            return result;
        }
        reverse.append({steps[i].to, temporaryName(steps[i].to, i), steps[i].from});
    }
    if (!runSteps(reverse, &result.error)) return result;
    writeJournal(journalPath, QStringLiteral("undone"), steps, created);
    result.ok = true;
    result.renamed = int(reverse.size());
    return result;
}

QString latestUndoableJournal(const QString& folder)
{
    QStringList journals = journalsIn(folder);
    for (auto it = journals.crbegin(); it != journals.crend(); ++it) {
        QString state;
        QVector<Step> steps;
        qint64 created = 0;
        if (readJournal(*it, &state, &steps, &created) && state == QLatin1String("done")) return *it;
    }
    return {};
}

QStringList interruptedJournals(const QString& folder)
{
    QStringList interrupted;
    for (const QString& path : journalsIn(folder)) {
        QString state;
        QVector<Step> steps;
        qint64 created = 0;
        if (readJournal(path, &state, &steps, &created) && state == QLatin1String("started")) interrupted << path;
    }
    return interrupted;
}

RenameResult recoverInterrupted(const QString& journalPath)
{
    RenameResult result;
    result.journalPath = journalPath;
    QString state;
    QVector<Step> steps;
    qint64 created = 0;
    if (!readJournal(journalPath, &state, &steps, &created) || state != QLatin1String("started")) {
        result.error = QStringLiteral("Nothing to recover.");
        return result;
    }
    QStringList stuck;
    for (const Step& s : steps) {
        if (QFileInfo::exists(s.from)) continue; // never moved
        if (QFileInfo::exists(s.temp) && moveFile(s.temp, s.from)) { ++result.renamed; continue; }
        if (QFileInfo::exists(s.to) && moveFile(s.to, s.from)) { ++result.renamed; continue; }
        stuck << QFileInfo(s.from).fileName();
    }
    if (!stuck.isEmpty()) {
        result.error = QStringLiteral("These could not be restored: %1").arg(stuck.join(QStringLiteral(", ")));
        return result;
    }
    writeJournal(journalPath, QStringLiteral("rolledBack"), steps, created);
    result.ok = true;
    return result;
}

} // namespace atk::review
