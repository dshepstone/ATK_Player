#pragma once

#include "export/ExportSpec.h"
#include <QWidget>

class QCheckBox;

namespace atk::ui {

class BurnInOptionsWidget final : public QWidget {
    Q_OBJECT
public:
    explicit BurnInOptionsWidget(QWidget* parent = nullptr);
    exporter::ExportBurnIns options() const;
    void setOptions(const exporter::ExportBurnIns& options);
private:
    QCheckBox* m_frameNumber = nullptr;
    QCheckBox* m_bookmarkLabels = nullptr;
    QCheckBox* m_bookmarkNotes = nullptr;
};

} // namespace atk::ui
