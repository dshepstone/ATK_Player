#pragma once

#include "project/Project.h"

#include <QWidget>

class QAction;
class QComboBox;
class QLabel;
class QToolButton;

namespace atk::ui {

/// The words and colour for a clip status, shared by the header and the
/// playlist cards so they always agree. Colour is never the only cue.
QString clipStatusLabel(project::ClipStatus status);
QColor clipStatusColor(project::ClipStatus status);

/// The strip above the picture: where you are (project › clip), the clip's
/// review status, and quick save / save as / export / show-in-folder buttons.
///
/// It shows what it is told and reports what the user picks; MainWindow owns
/// the project and applies the changes.
class ReviewHeaderBar final : public QWidget {
    Q_OBJECT

public:
    explicit ReviewHeaderBar(QWidget* parent = nullptr);

    /// No clip: the status and buttons are disabled and the clip name blank.
    void setLocation(const QString& projectName, const QString& clipName, bool hasClip);
    /// Shows `status` without emitting statusChosen.
    void setStatus(project::ClipStatus status);
    project::ClipStatus status() const;
    /// The Export menu offers these (the window's own export commands).
    void setExportActions(const QList<QAction*>& actions);
    /// A button for the window's "Rename Clips from Team List" command.
    void setRenameAction(QAction* action);
    /// Buttons for the window's Save Project and Save Project As commands.
    void setSaveActions(QAction* save, QAction* saveAs);

signals:
    void statusChosen(atk::project::ClipStatus status);
    void showInFolderRequested();
    /// The house button: back to the Review Library.
    void libraryRequested();

private:
    QToolButton* m_home = nullptr;
    QLabel* m_project = nullptr;
    QLabel* m_separator = nullptr;
    QLabel* m_clip = nullptr;
    QComboBox* m_status = nullptr;
    QToolButton* m_save = nullptr;
    QToolButton* m_saveAs = nullptr;
    QToolButton* m_export = nullptr;
    QToolButton* m_rename = nullptr;
    QToolButton* m_folder = nullptr;
    QToolButton* m_reveal = nullptr;
    bool m_updating = false;
};

} // namespace atk::ui
