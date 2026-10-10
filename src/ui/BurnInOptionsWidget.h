#pragma once

#include "export/ExportSpec.h"
#include <QWidget>

class QCheckBox;
class QSpinBox;

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
    QCheckBox* m_annotations = nullptr;
    QCheckBox* m_annotationNotes = nullptr;
    QSpinBox* m_holdFrames = nullptr;
};

} // namespace atk::ui
