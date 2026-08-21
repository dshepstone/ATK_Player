#pragma once

#include <QWidget>

class QListWidget;
class QLabel;
class QPushButton;

namespace atk::project { class Project; }

namespace atk::ui {

/// Compact project playlist panel. Selection and activation are intentionally
/// separate: a row loads only on double-click/Enter.
///
/// PHASE 0 STATUS: the panel and its empty state are real; the list stays empty
/// because nothing can be loaded until decoding exists. setProject() is already
/// wired so M1 only has to make loading succeed.
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

signals:
    /// A row was activated (double-click or Enter).
    void sourceActivated(int playlistIndex);
    void addMediaRequested();
    void removeRequested(int playlistIndex);
    void moveRequested(int fromIndex, int toIndex);

private:
    void refresh();

    project::Project* m_project = nullptr;
    /// Set while a single M1 source is loaded; overrides the project listing.
    bool m_showingSingleSource = false;
    QListWidget* m_list = nullptr;
    QLabel* m_emptyHint = nullptr;
    bool m_refreshing = false;
};

} // namespace atk::ui
