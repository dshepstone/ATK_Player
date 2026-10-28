#pragma once

#include "export/ExportSpec.h"
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace atk::ui {
class BurnInOptionsWidget;

class ExportDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(exporter::ExportSpec spec, QWidget* parent = nullptr);
    const exporter::ExportSpec& spec() const { return m_spec; }
    QString destination() const;
    exporter::ExportBurnIns burnIns() const;
    void setOpenWhenDone(bool open);
    bool openWhenDone() const;
    /// The project's remembered save location (empty when none is set).
    /// `projectSaved` is false for an untitled project, which keeps the
    /// location only once it is saved.
    void setProjectFolder(const QString& folder, bool projectSaved);
    QString projectFolder() const { return m_projectFolder; }
signals:
    /// The user set (or, with an empty folder, cleared) the project's save
    /// location. Exports of the project default there from now on.
    void projectFolderChanged(const QString& folder);
private:
    void browse();
    void chooseProjectFolder();
    void clearProjectFolder();
    void showProjectFolder();
    QString m_projectFolder;
    bool m_projectSaved = false;
    QLabel* m_projectFolderLabel = nullptr;
    QPushButton* m_clearProjectFolder = nullptr;
    exporter::ExportSpec m_spec;
    QLineEdit* m_destination = nullptr;
    QPushButton* m_export = nullptr;
    BurnInOptionsWidget* m_burnIns = nullptr;
    QCheckBox* m_openWhenDone = nullptr;
};

} // namespace atk::ui
