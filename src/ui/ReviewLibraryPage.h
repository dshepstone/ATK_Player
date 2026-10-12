#pragma once

#include "review/ReviewLibrary.h"

#include <QHash>
#include <QImage>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QVBoxLayout;

namespace atk::ui {

/// The Review Library start screen: the reviews this user has set up, each a
/// folder of clips, with the first few clips shown as a strip. Filter by All,
/// Starred or Archived; search and sort; start a new review from a folder.
///
/// It edits the library's own flags (star, archive, remove) and asks the
/// owner, through signals, to save, open a review or create one. It never
/// opens media or decodes: thumbnails are asked for with thumbnailWanted().
class ReviewLibraryPage final : public QWidget {
    Q_OBJECT

public:
    enum class Section { All, Starred, Archived };
    enum class Order { Latest, Name };

    /// `library` is not owned and must outlive the page.
    explicit ReviewLibraryPage(review::ReviewLibrary* library, QWidget* parent = nullptr);

    /// Rebuilds the list from the library.
    void refresh();
    void setSection(Section section);
    Section section() const { return m_section; }
    void setOrder(Order order);
    void setSearchText(const QString& text);

    /// Shows the picture for a clip in any strip that lists it.
    void setThumbnail(const QString& clipPath, const QImage& image);

    /// The review ids as listed, top to bottom (tests).
    QVector<QUuid> listedReviews() const { return m_listed; }

signals:
    void newReviewRequested();
    void openReviewRequested(const QUuid& id);
    void openProjectRequested();
    void openMediaRequested();
    void showFolderRequested(const QString& folder);
    /// The library changed (star, archive, remove); the owner saves it.
    void libraryChanged();
    void thumbnailWanted(const QString& clipPath);

private:
    QWidget* makeRow(const review::ReviewInfo& review);
    void showRowMenu(const QUuid& id, QWidget* anchor);

    review::ReviewLibrary* m_library = nullptr;
    Section m_section = Section::All;
    Order m_order = Order::Latest;
    QListWidget* m_sections = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_sort = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QLabel* m_empty = nullptr;
    QVector<QUuid> m_listed;
    QHash<QString, QImage> m_thumbnails;
    QHash<QString, QList<QLabel*>> m_thumbnailLabels;
};

} // namespace atk::ui
