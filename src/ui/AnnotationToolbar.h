#pragma once

#include "annotation/AnnotationToolSettings.h"

#include <QHash>
#include <QToolBar>

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QSlider;
class QSpinBox;
class QToolButton;

namespace atk::ui {

/// The drawing toolbar: tool, colour, brush size, opacity, pen pressure and
/// stabiliser.
///
/// It edits an AnnotationToolSettings value and reports changes; it holds no
/// annotation data and never talks to the viewer directly. MainWindow shows it
/// while Draw on Frame is on and forwards the settings to the viewer.
class AnnotationToolbar : public QToolBar {
    Q_OBJECT

public:
    explicit AnnotationToolbar(QWidget* parent = nullptr);

    const annotation::AnnotationToolSettings& settings() const { return m_settings; }
    /// Updates every control without emitting settingsChanged.
    void setSettings(const annotation::AnnotationToolSettings& settings);

    /// Preset swatches offered in the colour menu.
    static QList<QColor> presetColors();

signals:
    void settingsChanged(const atk::annotation::AnnotationToolSettings& settings);

private:
    void addToolAction(annotation::AnnotationTool tool, const QString& label, const QString& tip);
    void apply(const annotation::AnnotationToolSettings& settings, bool notify);
    void refreshColorButton();
    void chooseCustomColor();

    annotation::AnnotationToolSettings m_settings;
    QActionGroup* m_toolGroup = nullptr;
    QHash<int, QAction*> m_toolActions;
    QToolButton* m_colorButton = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QSpinBox* m_sizeSpin = nullptr;
    QSpinBox* m_opacitySpin = nullptr;
    QCheckBox* m_pressureCheck = nullptr;
    QComboBox* m_curveCombo = nullptr;
    QSpinBox* m_stabilizerSpin = nullptr;
    QComboBox* m_eraserModeCombo = nullptr;
    bool m_updating = false;
};

} // namespace atk::ui
