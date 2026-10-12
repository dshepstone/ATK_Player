#pragma once

#include "review/BatchRename.h"
#include "review/Roster.h"
#include "review/RosterMatcher.h"

#include <QDialog>
#include <QUuid>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;

namespace atk::ui {

/// "Rename Clips from Class List": match the clips of a review to a class or
/// staff list (CSV or Excel) and give them the people's names.
///
/// By default only the names shown in ATK Player change (the playlist,
/// notes PDF and exports use them); the files are untouched. Renaming the
/// files on disk is a deliberate second choice, checked in full before
/// anything is renamed, and undoable. The dialog only decides; MainWindow
/// carries it out.
class BatchRenameDialog final : public QDialog {
    Q_OBJECT

public:
    struct Clip {
        QUuid id;
        QString path;        ///< the file (or a sequence's pattern)
        QString displayName; ///< as shown now
        bool sequence = false;
    };

    using Decision = review::RenameDecision;

    BatchRenameDialog(QVector<Clip> clips, QString reviewName, QString nameTemplate, QWidget* parent = nullptr);

    /// Loads a class list (also what "Choose List…" does).
    bool loadRoster(const QString& path);
    /// Or uses one already read (tests).
    void setRoster(const review::Roster& roster);

    QString rosterPath() const { return m_rosterPath; }
    QString nameTemplate() const;
    bool renameOnDisk() const;
    bool orderByRoster() const;

    /// One per clip, in the review's (possibly new) order.
    QVector<Decision> decisions() const;
    /// The checked plan for renameOnDisk(); empty items otherwise.
    review::RenamePlan diskPlan() const;

    /// The person a clip is matched to (row in the list), -1 for none, and
    /// changing it by hand.
    int matchFor(int clipIndex) const;
    void setMatch(int clipIndex, int rosterRow);

    /// Set by the "Undo Last Rename" button: the dialog closes with this
    /// journal for the owner to undo.
    QString undoJournal() const { return m_undoJournal; }

private:
    void chooseRoster();
    void applyColumns();
    void rematch();
    void rebuildTable();
    void refreshNames();
    QVector<int> order() const;
    review::NameContext contextFor(int clipIndex, int position) const;

    QVector<Clip> m_clips;
    QString m_reviewName;
    QString m_rosterPath;
    review::Roster m_roster;
    review::RosterColumns m_columns;
    QVector<review::RosterMatch> m_matches;

    QLabel* m_rosterLabel = nullptr;
    QCheckBox* m_headerRow = nullptr;
    QComboBox* m_idColumn = nullptr;
    QComboBox* m_firstColumn = nullptr;
    QComboBox* m_lastColumn = nullptr;
    QComboBox* m_fullColumn = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_missing = nullptr;
    QLineEdit* m_template = nullptr;
    QRadioButton* m_displayOnly = nullptr;
    QRadioButton* m_onDisk = nullptr;
    QCheckBox* m_order = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_undo = nullptr;
    QString m_undoJournal;
    bool m_updating = false;
};

} // namespace atk::ui
