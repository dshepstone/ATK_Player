#pragma once

#include <QHash>
#include <QImage>
#include <QSet>
#include <QUuid>
#include <QWidget>

class QComboBox;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;

namespace atk::project { class Project; struct SourceEntry; }

namespace atk::ui {

/// Project playlist panel: one card per clip with a thumbnail, its details and
/// note/drawing counts (see PlaylistCardDelegate). Selection and activation
/// are intentionally separate: a row loads only on double-click/Enter. Rows
/// can be dragged to reorder.
///
/// It never decodes: it asks for thumbnails with thumbnailWanted() and shows
/// what the owner hands back through setThumbnail().
class SourcesPanel : public QWidget {
    Q_OBJECT

public:
    explicit SourcesPanel(QWidget* parent = nullptr);
    ~SourcesPanel() override;

    /// Observes a project; not owned. Passing nullptr detaches.
    void setProject(project::Project* project);

    /// Shows the single currently-loaded clip.
    ///
    /// M1 plays one source at a time, so this replaces whatever was listed
    /// rather than appending. Playlists arrive in M3.
    void setCurrentMedia(const QString& fileName, const QString& description);

    /// Returns to the empty state.
    void clearCurrentMedia();
    int selectedIndex() const;

    /// Shows the picture for a clip; a null image clears it.
    void setThumbnail(const QUuid& sourceId, const QImage& image);
    QImage thumbnail(const QUuid& sourceId) const { return m_thumbnails.value(sourceId); }
    QListWidget* list() const { return m_list; }
    /// Shows only the clips in `category` (an empty name: clips with none),
    /// or every clip with a null string. Clips keep their place in the order.
    void setCategoryFilter(const QString& category);
    QString categoryFilter() const { return m_categoryFilter; }
    /// True when the filter shows clips in `category`.
    bool showsCategory(const QString& category) const
    {
        return m_categoryFilter.isNull() || category == m_categoryFilter;
    }
    QComboBox* categoryFilterBox() const { return m_filterBox; }

    /// Wide enough for a thumbnail card and a readable name. The dock starts
    /// at this width; a saved layout keeps whatever width the user chose.
    QSize sizeHint() const override { return QSize(300, QWidget::sizeHint().height()); }

signals:
    /// A row was activated (double-click or Enter).
    void sourceActivated(int playlistIndex);
    void addMediaRequested();
    void removeRequested(int playlistIndex);
    void moveRequested(int fromIndex, int toIndex);
    /// The Arrange button: bulk reorder, sort and categories.
    void arrangeRequested();
    /// File a clip under a category (empty for none) from the context menu.
    void categoryRequested(int playlistIndex, const QString& category);
    void relinkRequested(int playlistIndex);
    /// Change the playback rate of an image sequence or still.
    void frameRateRequested(int playlistIndex);
    /// A readable clip needs a thumbnail. Asked once per clip and file path.
    void thumbnailWanted(const QUuid& sourceId);

private:
    void refresh();
    void updateCounts(QListWidgetItem* item, const project::SourceEntry& entry);
    void updateCounts(const QUuid& sourceId);
    /// Hides the clips the filter leaves out and puts a heading above each
    /// run of clips in one category, when the playlist uses categories.
    void applyCategoryLayout();
    void refreshFilterChoices();

    project::Project* m_project = nullptr;
    /// Set while a single M1 source is loaded; overrides the project listing.
    bool m_showingSingleSource = false;
    QListWidget* m_list = nullptr;
    QLabel* m_emptyHint = nullptr;
    QLabel* m_dropHint = nullptr;
    QHash<QUuid, QImage> m_thumbnails;
    /// Source id + path already asked for, so a refresh does not ask again but
    /// a relinked clip does.
    QSet<QString> m_thumbnailsRequested;
    bool m_refreshing = false;
    QWidget* m_filterRow = nullptr;
    QComboBox* m_filterBox = nullptr;
    /// Null: all clips. Empty: clips with no category. Otherwise a category.
    QString m_categoryFilter;
};

} // namespace atk::ui
