#pragma once

#include "project/Project.h"

#include <QDateTime>
#include <QDialog>
#include <QStringList>
#include <QUuid>
#include <QVector>

class QComboBox;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace atk::ui {

/// Bulk editing of the playlist's order: move the selected clips up, down, to
/// the top or bottom (or drag them), sort the whole list by name, file date,
/// length, category or review status, and file clips under categories the
/// reviewer names. Nothing changes in the project until Apply.
class ArrangePlaylistDialog final : public QDialog {
    Q_OBJECT
public:
    /// One clip, as the dialog lists it.
    struct Clip {
        QUuid id;
        QString name;
        /// File date (last modified); invalid when unknown.
        QDateTime modified;
        /// Length in microseconds; negative when unknown.
        qint64 durationUs = -1;
        QString category;
        project::ClipStatus status = project::ClipStatus::NotReviewed;
    };

    enum class SortKey {
        NameAscending,
        NameDescending,
        DateOldestFirst,
        DateNewestFirst,
        LengthShortestFirst,
        LengthLongestFirst,
        Category,
        Status,
        Reverse,
    };

    ArrangePlaylistDialog(QVector<Clip> clips, QStringList categories, QWidget* parent = nullptr);

    /// The clips in the arranged order, with their categories.
    QVector<Clip> clips() const;
    QVector<QUuid> order() const;
    QStringList categories() const { return m_categories; }

    /// Sorts the whole list; the clips' relative order is kept where the key
    /// is equal. Names compare naturally ("shot2" before "shot10").
    void sortBy(SortKey key);
    /// Moves the selected clips together: by one place (`delta` -1 or +1), or
    /// to the top or bottom (`delta` beyond the list).
    void moveSelected(int delta);
    /// Files the selected clips under `category` (added to the list when new),
    /// or under none with an empty name.
    void setSelectedCategory(const QString& category);
    /// Selects clips by row, for tests and keyboard use.
    void selectRows(const QVector<int>& rows);

    /// The sort used by sortBy(), on its own: stable, categories in the order
    /// of `categories` with uncategorized clips last.
    static void sortClips(QVector<Clip>& clips, SortKey key, const QStringList& categories);

    QTreeWidget* list() const { return m_list; }

private:
    void fill(const QVector<Clip>& clips);
    void renumber();
    void refreshCategoryChoices();
    void refreshButtons();
    void addCategory();
    void removeCategory();
    void renameCategory();

    QTreeWidget* m_list = nullptr;
    QComboBox* m_category = nullptr;
    QPushButton* m_removeCategory = nullptr;
    QPushButton* m_renameCategory = nullptr;
    QPushButton* m_setCategory = nullptr;
    QList<QPushButton*> m_moveButtons;
    QStringList m_categories;
};

} // namespace atk::ui
