#pragma once

#include <QHash>
#include <QImage>
#include <QSet>
#include <QUuid>
#include <QWidget>

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

    /// Wide enough for a thumbnail card and a readable name. The dock starts
    /// at this width; a saved layout keeps whatever width the user chose.
    QSize sizeHint() const override { return QSize(300, QWidget::sizeHint().height()); }

signals:
    /// A row was activated (double-click or Enter).
    void sourceActivated(int playlistIndex);
    void addMediaRequested();
    void removeRequested(int playlistIndex);
    void moveRequested(int fromIndex, int toIndex);
    void relinkRequested(int playlistIndex);
    /// Change the playback rate of an image sequence or still.
    void frameRateRequested(int playlistIndex);
    /// A readable clip needs a thumbnail. Asked once per clip and file path.
    void thumbnailWanted(const QUuid& sourceId);

private:
    void refresh();
    void updateCounts(QListWidgetItem* item, const project::SourceEntry& entry);
    void updateCounts(const QUuid& sourceId);

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
};

} // namespace atk::ui
