#pragma once

#include <QWidget>

class QListWidget;
class QLabel;

namespace atk::project { class Project; }

namespace atk::ui {

/// The left-hand SOURCES panel: the list of media loaded into the session.
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

signals:
    /// A row was activated (double-click or Enter).
    void sourceActivated(int playlistIndex);

private:
    void refresh();

    project::Project* m_project = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_emptyHint = nullptr;
};

} // namespace atk::ui
