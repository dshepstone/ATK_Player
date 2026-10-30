#pragma once

#include "export/ExportSpec.h"
#include <QDialog>

#include <optional>

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
    /// The burn-in choices kept with the review, shown in place of the
    /// defaults (the spec's) when there are some. "Use Defaults" goes back.
    void setSavedBurnIns(const std::optional<exporter::ExportBurnIns>& saved);
    /// True when the choices made here should be kept with the review for
    /// its next exports. On by default.
    bool rememberBurnIns() const;
    void setRememberBurnIns(bool remember);
    /// True after "Use Defaults": this export uses the standard choices and
    /// the review keeps the ones it has saved.
    bool defaultsForThisExportOnly() const { return m_defaultsForThisExport; }
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
    QCheckBox* m_rememberBurnIns = nullptr;
    QLabel* m_savedBurnInsNote = nullptr;
    QPushButton* m_defaultBurnIns = nullptr;
    bool m_defaultsForThisExport = false;
    QCheckBox* m_openWhenDone = nullptr;
};

} // namespace atk::ui
