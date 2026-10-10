#pragma once

#include "export/ExportSpec.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace atk::ui {

class BurnInOptionsWidget final : public QWidget {
    Q_OBJECT
public:
    /// What happens after each drawn or noted frame in a video export.
    enum class AfterNote { PlayThrough, KeepOnScreen, Freeze };

    explicit BurnInOptionsWidget(QWidget* parent = nullptr);
    exporter::ExportBurnIns options() const;
    void setOptions(const exporter::ExportBurnIns& options);
    /// Hides the play-through / hold / freeze row (still image exports).
    void setVideoOptionsVisible(bool visible);
private:
    void refreshEnabled();

    QCheckBox* m_frameNumber = nullptr;
    QCheckBox* m_bookmarkLabels = nullptr;
    QCheckBox* m_bookmarkNotes = nullptr;
    QCheckBox* m_annotations = nullptr;
    QCheckBox* m_annotationNotes = nullptr;
    QWidget* m_afterRow = nullptr;
    QComboBox* m_afterNote = nullptr;
    QSpinBox* m_holdFrames = nullptr;
    QComboBox* m_tracingPaper = nullptr;
};

} // namespace atk::ui
