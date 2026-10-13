#pragma once

#include "annotation/AnnotationGhosting.h"
#include "annotation/AnnotationToolSettings.h"
#include "annotation/BrushLibrary.h"

#include <QHash>
#include <QPointer>
#include <QToolBar>

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QToolButton;

namespace atk::ui {

class BrushSizePill;

/// The drawing toolbar: one row of icon buttons for the tools, colour, the
/// owner's edit actions (undo, redo, clear), tracing paper and ghosting, and a
/// "tool options" pop-up for eraser mode, pen pressure and stabiliser. Brush
/// size and opacity live on a BrushSizePill beside the picture.
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

    /// Brush size and opacity are edited on `pill`, kept in step with these
    /// settings. The pill is not owned; it may be destroyed first.
    void attachSizePill(BrushSizePill* pill);

    /// Adds the owner's actions (for example undo, redo and clear) to the row,
    /// after the colour. The toolbar shows them as icons; it does not own them.
    void addEditActions(const QList<QAction*>& actions);

    /// The button that opens eraser mode, pressure and stabiliser options.
    QToolButton* toolOptionsButton() const { return m_optionsButton; }

    /// Rebuilds the Brushes menu from the imported sets.
    void setBrushSets(const QVector<annotation::BrushSet>& sets);

    /// Shows the tracing paper state (0 = off) and the level its toggle uses,
    /// and the ghosting settings, without emitting.
    void setTracingPaper(int percent, int level);
    void setGhostSettings(const annotation::GhostSettings& settings);
    QToolButton* tracingPaperButton() const { return m_traceButton; }
    /// Tracing paper and ghosts are view settings, useful outside Draw on
    /// Frame too: the toolbar keeps their state and menus, and the owner shows
    /// these buttons in the transport row (TransportControls::addViewTools).
    QList<QWidget*> viewTools() const;
    QToolButton* ghostButton() const { return m_ghostButton; }

signals:
    void settingsChanged(const atk::annotation::AnnotationToolSettings& settings);
    void importBrushesRequested();
    void removeBrushSetRequested(const QString& setName);
    /// Tracing paper turned on/off or set to a level (percent, 0 = off).
    void tracingPaperRequested(int percent);
    void ghostSettingsRequested(const atk::annotation::GhostSettings& settings);

private:
    void addToolAction(annotation::AnnotationTool tool, const QString& iconName, const QString& label,
                       const QString& tip);
    QToolButton* addIconButton(QToolButton* button, const QString& iconName);
    void apply(const annotation::AnnotationToolSettings& settings, bool notify);
    void refreshColorButton();
    void chooseCustomColor();
    void refreshBrushButton();

    annotation::AnnotationToolSettings m_settings;
    QActionGroup* m_toolGroup = nullptr;
    QHash<int, QAction*> m_toolActions;
    QToolButton* m_colorButton = nullptr;
    QPointer<BrushSizePill> m_pill;
    QAction* m_editAnchor = nullptr;
    QToolButton* m_optionsButton = nullptr;
    QCheckBox* m_pressureCheck = nullptr;
    QComboBox* m_curveCombo = nullptr;
    QSpinBox* m_stabilizerSpin = nullptr;
    QComboBox* m_eraserModeCombo = nullptr;
    QToolButton* m_brushButton = nullptr;
    QVector<annotation::BrushSet> m_brushSets;
    QToolButton* m_traceButton = nullptr;
    QActionGroup* m_traceLevels = nullptr;
    int m_traceLevel = 50;
    QToolButton* m_ghostButton = nullptr;
    QSpinBox* m_ghostBefore = nullptr;
    QSpinBox* m_ghostAfter = nullptr;
    QAction* m_ghostCountFrames = nullptr;
    QAction* m_ghostCountDrawings = nullptr;
    QAction* m_ghostTint = nullptr;
    annotation::GhostSettings m_ghost;
    bool m_updating = false;
};

} // namespace atk::ui
