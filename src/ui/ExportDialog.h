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
private:
    void browse();
    exporter::ExportSpec m_spec;
    QLineEdit* m_destination = nullptr;
    QPushButton* m_export = nullptr;
    BurnInOptionsWidget* m_burnIns = nullptr;
    QCheckBox* m_openWhenDone = nullptr;
};

} // namespace atk::ui
