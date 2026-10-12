#pragma once

#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace atk::review {

/// What to do with one clip of a review when renaming from a class list.
struct RenameDecision {
    QUuid id;            ///< the project entry
    int rosterRow = -1;  ///< the person, or -1: not matched, left as it is
    QString newName;     ///< without extension; empty: unchanged
    QString newPath;     ///< for a rename on disk: the new file; else empty
};

/// One file to rename: absolute paths.
struct RenameItem {
    QString from;
    QString to;
};

/// Why a planned rename cannot go ahead.
enum class RenameProblem {
    None,
    Unchanged,       ///< same name: skipped, not an error
    SourceMissing,
    EmptyName,
    IllegalName,     ///< characters or names no OS allows everywhere
    PathTooLong,
    DuplicateTarget, ///< two clips would get the same name
    TargetExists,    ///< a file that is not being renamed already has it
};

QString renameProblemText(RenameProblem problem);

struct PlannedRename {
    QString from;
    QString to;
    RenameProblem problem = RenameProblem::None;
};

struct RenamePlan {
    QVector<PlannedRename> items;
    /// True when nothing blocks the plan (unchanged items are skipped).
    bool ok() const;
    int changeCount() const;
};

/// Checks every rename before anything is touched. Names that differ only
/// in case count as the same name on Windows and macOS (`caseInsensitive`),
/// so a plan that is safe there is safe everywhere.
RenamePlan planRenames(const QVector<RenameItem>& items, bool caseInsensitive = true);

struct RenameResult {
    bool ok = false;
    QString error;
    QString journalPath;
    int renamed = 0;
};

/// Carries out a checked plan safely:
///   1. writes a journal (.atk-rename-<time>.json) beside the files first;
///   2. moves every file to a unique temporary name, then each to its new
///      name, so swaps and chains (A->B, B->A) can never overwrite anything;
///   3. on any failure, puts every file already moved back where it was.
/// Nothing is ever overwritten. The journal stays, for undo.
RenameResult executeRenames(const RenamePlan& plan);

/// Reverses the renames a finished journal records (the same two-step way).
RenameResult undoRenames(const QString& journalPath);

/// The renames a journal records (from -> to) and its state ("started",
/// "done", "undone", "rolledBack"). False when it cannot be read.
bool readRenameJournal(const QString& journalPath, QVector<RenameItem>* items, QString* state);

/// The newest finished, not yet undone journal in `folder`, or empty.
QString latestUndoableJournal(const QString& folder);

/// Journals in `folder` from a rename that never finished (a crash or power
/// cut part way). recoverInterrupted() restores the original names.
QStringList interruptedJournals(const QString& folder);
RenameResult recoverInterrupted(const QString& journalPath);

} // namespace atk::review
