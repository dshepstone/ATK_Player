#include "ui/AnnotationToolbar.h"

#include "ui/BrushSizePill.h"
#include "ui/IconProvider.h"
#include "ui/Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QFormLayout>
#include <QSpinBox>
#include <QToolButton>
#include <QWidgetAction>

#include <algorithm>

namespace atk::ui {
namespace {
using annotation::AnnotationTool;

/// A colour swatch, drawn at both scales so it stays crisp on high-DPI screens.
QIcon swatchIcon(const QColor& color, int size = 16)
{
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pixmap(size * scale, size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1.0));
        painter.setBrush(color);
        painter.drawEllipse(QRectF(1.5, 1.5, size - 3.0, size - 3.0));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

// Flat icon buttons: no frame until hovered, a filled accent tile when the
// tool is chosen. Applied to the whole row so QToolBar's own buttons match.
QString toolRowStyleSheet()
{
    return QStringLiteral(R"CSS(
QToolBar#annotationToolbar { spacing: 2px; padding: 3px 6px; }
QToolBar#annotationToolbar QToolButton {
    background-color: transparent;
    border: 1px solid transparent;
    border-radius: %1px;
    padding: 4px;
    min-width: 0;
}
QToolBar#annotationToolbar QToolButton:hover   { background-color: %2; }
QToolBar#annotationToolbar QToolButton:pressed { background-color: %3; }
QToolBar#annotationToolbar QToolButton:checked { background-color: %4; border-color: %4; }
QToolBar#annotationToolbar QToolButton[popupMode="1"] { padding-right: 12px; }
QToolBar#annotationToolbar QToolButton::menu-button {
    background: transparent;
    border: none;
    width: 12px;
}
QToolBar#annotationToolbar QToolButton::menu-button:hover { background-color: %3; }
/* Pop-up buttons say so with their own icon (chevron, swatch, sliders). */
QToolBar#annotationToolbar QToolButton[popupMode="2"]::menu-indicator { image: none; width: 0; }
QToolBar#annotationToolbar::separator { background: %5; width: 1px; margin: 6px 6px; }
)CSS")
        .arg(theme::metrics::radiusSmall)
        .arg(theme::controlHover().name(), theme::controlPressed().name(), theme::accentFill().name(),
             theme::controlHover().name());
}
} // namespace

AnnotationToolbar::AnnotationToolbar(QWidget* parent) : QToolBar(tr("Drawing"), parent)
{
    setObjectName(QStringLiteral("annotationToolbar"));
    setMovable(true);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(theme::metrics::iconLarge, theme::metrics::iconLarge));
    setStyleSheet(toolRowStyleSheet());

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    addToolAction(AnnotationTool::Select, QStringLiteral("mouse-pointer-2"), tr("Select"),
                  tr("Select: click a drawing or text to select it, drag to move it, Delete to remove it, "
                     "double-click text to edit it"));
    addSeparator();
    addToolAction(AnnotationTool::Pencil, QStringLiteral("pencil"), tr("Pencil"),
                  tr("Pencil: grainy, pressure sets width and darkness"));
    addToolAction(AnnotationTool::Pen, QStringLiteral("pen-tool"), tr("Pen"), tr("Pen: smooth ink, pressure sets width"));
    addToolAction(AnnotationTool::Brush, QStringLiteral("brush"), tr("Brush"),
                  tr("Brush: soft edge, pressure sets width and opacity"));
    addToolAction(AnnotationTool::Marker, QStringLiteral("highlighter"), tr("Marker"), tr("Marker: flat, even, translucent"));
    addToolAction(AnnotationTool::Stamp, QStringLiteral("paintbrush"), tr("Custom"),
                  tr("Custom brush: paints with an imported brush tip (for example from a Photoshop .abr set)"));
    // Choose brush: the imported brush library, right after Custom. A fixed
    // icon, so it always reads as "pick a brush"; the brushes themselves are
    // previewed in its menu.
    m_brushButton = new QToolButton(this);
    m_brushButton->setObjectName(QStringLiteral("BrushSetButton"));
    m_brushButton->setText(tr("Choose brush"));
    m_brushButton->setPopupMode(QToolButton::InstantPopup);
    m_brushButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_brushButton->setMenu(new QMenu(m_brushButton));
    addIconButton(m_brushButton, QStringLiteral("library-big"));
    addToolAction(AnnotationTool::Eraser, QStringLiteral("eraser"), tr("Eraser"),
                  tr("Eraser (the pen's eraser end works too). Area or whole-stroke mode is under Tool options."));
    m_eraserModeCombo = new QComboBox;
    m_eraserModeCombo->addItem(tr("Erase area"), int(annotation::EraserMode::Area));
    m_eraserModeCombo->addItem(tr("Erase stroke"), int(annotation::EraserMode::WholeStroke));
    m_eraserModeCombo->setObjectName(QStringLiteral("EraserModeCombo"));
    m_eraserModeCombo->setToolTip(tr("Erase area removes only the ink under the eraser; "
                                     "Erase stroke removes every stroke it touches"));
    connect(m_eraserModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0) return;
        auto next = m_settings;
        next.eraserMode = annotation::EraserMode(m_eraserModeCombo->itemData(index).toInt());
        apply(next, true);
    });
    addSeparator();
    addToolAction(AnnotationTool::Arrow, QStringLiteral("arrow-up-right"), tr("Arrow"), tr("Arrow"));
    addToolAction(AnnotationTool::Rectangle, QStringLiteral("square"), tr("Rect"), tr("Rectangle"));
    addToolAction(AnnotationTool::Ellipse, QStringLiteral("circle"), tr("Ellipse"), tr("Ellipse"));
    addToolAction(AnnotationTool::Line, QStringLiteral("minus"), tr("Line"), tr("Straight line"));
    addToolAction(AnnotationTool::Text, QStringLiteral("type"), tr("Text"),
                  tr("Text: click to type at a point, or drag a box for text that wraps. "
                     "Ctrl+Enter or click away to finish, Esc to cancel"));
    addSeparator();

    m_colorButton = new QToolButton(this);
    m_colorButton->setObjectName(QStringLiteral("ColorButton"));
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
    m_colorButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto* colorMenu = new QMenu(m_colorButton);
    for (const QColor& color : presetColors()) {
        QAction* swatch = colorMenu->addAction(swatchIcon(color), color.name());
        connect(swatch, &QAction::triggered, this, [this, color] {
            auto next = m_settings;
            next.color = color;
            apply(next, true);
        });
    }
    colorMenu->addSeparator();
    connect(colorMenu->addAction(tr("Custom...")), &QAction::triggered, this, &AnnotationToolbar::chooseCustomColor);
    m_colorButton->setMenu(colorMenu);
    addWidget(m_colorButton);

    // The owner's undo, redo and clear go here (addEditActions).
    m_editAnchor = addSeparator();

    // --- Tool options pop-up: settings changed now and then, not per stroke.
    m_optionsButton = new QToolButton(this);
    m_optionsButton->setObjectName(QStringLiteral("ToolOptionsButton"));
    m_optionsButton->setText(tr("Tool options"));
    m_optionsButton->setToolTip(tr("Tool options: eraser mode, pen pressure and stabiliser"));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    auto* optionsMenu = new QMenu(m_optionsButton);
    optionsMenu->setObjectName(QStringLiteral("ToolOptionsMenu"));
    auto* optionsPanel = new QWidget(optionsMenu);
    auto* form = new QFormLayout(optionsPanel);
    form->setContentsMargins(12, 10, 12, 10);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    m_eraserModeCombo->setParent(optionsPanel);
    form->addRow(tr("Eraser"), m_eraserModeCombo);
    m_pressureCheck = new QCheckBox(tr("Use pen pressure"), optionsPanel);
    m_pressureCheck->setToolTip(tr("Use pen pressure from a drawing tablet"));
    form->addRow(tr("Pressure"), m_pressureCheck);
    connect(m_pressureCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        auto next = m_settings;
        next.usePressure = on;
        apply(next, true);
    });
    m_curveCombo = new QComboBox(optionsPanel);
    m_curveCombo->addItem(tr("Soft"), int(annotation::PressureCurve::Soft));
    m_curveCombo->addItem(tr("Linear"), int(annotation::PressureCurve::Linear));
    m_curveCombo->addItem(tr("Firm"), int(annotation::PressureCurve::Firm));
    m_curveCombo->setToolTip(tr("Pressure response: Soft needs a lighter touch, Firm a heavier one"));
    form->addRow(tr("Response"), m_curveCombo);
    connect(m_curveCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0) return;
        auto next = m_settings;
        next.pressureCurve = annotation::PressureCurve(m_curveCombo->itemData(index).toInt());
        apply(next, true);
    });

    m_stabilizerSpin = new QSpinBox(optionsPanel);
    m_stabilizerSpin->setRange(0, 95);
    m_stabilizerSpin->setSuffix(QStringLiteral("%"));
    m_stabilizerSpin->setToolTip(tr("Smooths shaky lines; higher values lag behind the pointer more"));
    form->addRow(tr("Stabiliser"), m_stabilizerSpin);
    connect(m_stabilizerSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating) return;
        auto next = m_settings;
        next.stabilizer = value / 100.0;
        apply(next, true);
    });
    auto* optionsAction = new QWidgetAction(optionsMenu);
    optionsAction->setDefaultWidget(optionsPanel);
    optionsMenu->addAction(optionsAction);
    m_optionsButton->setMenu(optionsMenu);

    // Tracing paper: a white wash under the drawings, like SyncSketch's.
    m_traceButton = new QToolButton(this);
    m_traceButton->setObjectName(QStringLiteral("TracingPaperButton"));
    m_traceButton->setText(tr("Trace"));
    m_traceButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_traceButton->setCheckable(true);
    m_traceButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_traceButton->setToolTip(tr("Tracing paper: fade the picture so drawings stand out (T). "
                                 "The arrow sets how strong it is."));
    auto* traceMenu = new QMenu(m_traceButton);
    traceMenu->addSection(tr("Trace opacity"));
    m_traceLevels = new QActionGroup(traceMenu);
    m_traceLevels->setExclusive(true);
    for (int level : {100, 75, 50, 25, 0}) {
        QAction* action = traceMenu->addAction(level == 0 ? tr("Off") : QStringLiteral("%1%").arg(level));
        action->setCheckable(true);
        action->setData(level);
        m_traceLevels->addAction(action);
        connect(action, &QAction::triggered, this, [this, level] { emit tracingPaperRequested(level); });
    }
    m_traceButton->setMenu(traceMenu);
    m_traceButton->setIcon(icons::icon(QStringLiteral("layers")));
    m_traceButton->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    m_traceButton->setAccessibleName(m_traceButton->text());
    m_traceButton->hide(); // shown where the owner places it (see viewTools())
    connect(m_traceButton, &QToolButton::clicked, this,
            [this](bool on) { emit tracingPaperRequested(on ? m_traceLevel : 0); });

    // Ghosting: drawings from nearby frames, faintly (onion skin).
    m_ghostButton = new QToolButton(this);
    m_ghostButton->setObjectName(QStringLiteral("GhostButton"));
    m_ghostButton->setText(tr("Ghost"));
    m_ghostButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_ghostButton->setCheckable(true);
    m_ghostButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_ghostButton->setToolTip(tr("Ghost drawings from earlier frames (red) and later frames (green) (G). "
                                 "The arrow sets how many frames each way."));
    auto* ghostMenu = new QMenu(m_ghostButton);
    ghostMenu->addSection(tr("Ghost drawings"));
    auto spinRow = [&](const QString& label, QSpinBox*& spin, const QString& objectName) {
        auto* row = new QWidget(ghostMenu);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(22, 3, 10, 3);
        layout->addWidget(new QLabel(label, row), 1);
        spin = new QSpinBox(row);
        spin->setObjectName(objectName);
        spin->setRange(0, annotation::GhostSettings::kMaxRange);
        layout->addWidget(spin);
        auto* action = new QWidgetAction(ghostMenu);
        action->setDefaultWidget(row);
        ghostMenu->addAction(action);
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            if (m_updating) return;
            auto next = m_ghost;
            next.before = m_ghostBefore->value();
            next.after = m_ghostAfter->value();
            next.enabled = true; // changing the reach means "show me"
            emit ghostSettingsRequested(next);
        });
    };
    spinRow(tr("Frames before"), m_ghostBefore, QStringLiteral("GhostBefore"));
    spinRow(tr("Frames after"), m_ghostAfter, QStringLiteral("GhostAfter"));
    ghostMenu->addSeparator();
    auto* countGroup = new QActionGroup(ghostMenu);
    m_ghostCountFrames = ghostMenu->addAction(tr("Count timeline frames"));
    m_ghostCountFrames->setToolTip(tr("Ghost every drawing within that many frames"));
    m_ghostCountDrawings = ghostMenu->addAction(tr("Count drawn frames"));
    m_ghostCountDrawings->setToolTip(tr("Ghost that many drawn frames each way, however far apart"));
    for (QAction* action : {m_ghostCountFrames, m_ghostCountDrawings}) {
        action->setCheckable(true);
        countGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this] {
            auto next = m_ghost;
            next.mode = m_ghostCountDrawings->isChecked() ? annotation::GhostSettings::Mode::Drawings
                                                          : annotation::GhostSettings::Mode::Frames;
            emit ghostSettingsRequested(next);
        });
    }
    ghostMenu->addSeparator();
    m_ghostTint = ghostMenu->addAction(tr("Tint (red before, green after)"));
    m_ghostTint->setCheckable(true);
    connect(m_ghostTint, &QAction::triggered, this, [this](bool on) {
        auto next = m_ghost;
        next.tint = on;
        emit ghostSettingsRequested(next);
    });
    m_ghostButton->setMenu(ghostMenu);
    m_ghostButton->setIcon(icons::icon(QStringLiteral("ghost")));
    m_ghostButton->setIconSize(QSize(theme::metrics::iconMedium, theme::metrics::iconMedium));
    m_ghostButton->setAccessibleName(m_ghostButton->text());
    m_ghostButton->hide();
    addIconButton(m_optionsButton, QStringLiteral("sliders-horizontal"));
    connect(m_ghostButton, &QToolButton::clicked, this, [this](bool on) {
        auto next = m_ghost;
        next.enabled = on;
        emit ghostSettingsRequested(next);
    });

    setBrushSets({});
    apply(m_settings, false);
    setTracingPaper(0, m_traceLevel);
    setGhostSettings(m_ghost);
}

void AnnotationToolbar::setTracingPaper(int percent, int level)
{
    m_traceLevel = level > 0 ? level : 50;
    QSignalBlocker blocker(m_traceButton);
    m_traceButton->setChecked(percent > 0);
    m_traceButton->setToolTip(percent > 0 ? tr("Tracing paper on at %1% (T). The arrow sets how strong it is.").arg(percent)
                                          : tr("Tracing paper: fade the picture so drawings stand out (T). "
                                               "The arrow sets how strong it is."));
    for (QAction* action : m_traceLevels->actions()) action->setChecked(action->data().toInt() == percent);
}

void AnnotationToolbar::setGhostSettings(const annotation::GhostSettings& settings)
{
    m_ghost = settings;
    const bool wasUpdating = m_updating;
    m_updating = true;
    {
        QSignalBlocker blocker(m_ghostButton);
        m_ghostButton->setChecked(settings.enabled);
    }
    m_ghostButton->setToolTip(tr("Ghost drawings from %1 frame(s) before (red) and %2 after (green) (G). "
                                 "The arrow sets how many.").arg(settings.before).arg(settings.after));
    m_ghostBefore->setValue(settings.before);
    m_ghostAfter->setValue(settings.after);
    m_ghostCountFrames->setChecked(settings.mode == annotation::GhostSettings::Mode::Frames);
    m_ghostCountDrawings->setChecked(settings.mode == annotation::GhostSettings::Mode::Drawings);
    m_ghostTint->setChecked(settings.tint);
    m_updating = wasUpdating;
}

void AnnotationToolbar::setBrushSets(const QVector<annotation::BrushSet>& sets)
{
    m_brushSets = sets;
    QMenu* menu = m_brushButton->menu();
    menu->clear();
    if (!sets.isEmpty()) menu->addSection(tr("Choose a brush"));
    for (const annotation::BrushSet& set : sets) {
        QMenu* sub = menu->addMenu(QStringLiteral("%1 (%2)").arg(set.name).arg(set.tips.size()));
        for (const annotation::BrushTip& tip : set.tips) {
            // Preview: the tip in dark ink on white, as it would print.
            QImage preview = tip.mask.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            preview.invertPixels();
            QAction* action = sub->addAction(QIcon(QPixmap::fromImage(preview)),
                                             tip.name.isEmpty() ? tr("Brush") : tip.name);
            action->setCheckable(true);
            action->setChecked(tip.id == m_settings.brushTipId);
            const QString id = tip.id;
            connect(action, &QAction::triggered, this, [this, id] {
                auto next = m_settings;
                next.brushTipId = id;
                next.tool = AnnotationTool::Stamp;
                apply(next, true);
            });
        }
    }
    if (!sets.isEmpty()) menu->addSeparator();
    connect(menu->addAction(tr("Import Photoshop Brushes (.abr)...")), &QAction::triggered, this,
            &AnnotationToolbar::importBrushesRequested);
    if (!sets.isEmpty()) {
        QMenu* remove = menu->addMenu(tr("Remove Brush Set"));
        for (const annotation::BrushSet& set : sets) {
            const QString name = set.name;
            connect(remove->addAction(name), &QAction::triggered, this,
                    [this, name] { emit removeBrushSetRequested(name); });
        }
    }
    refreshBrushButton();
}

void AnnotationToolbar::refreshBrushButton()
{
    if (!m_brushButton) return;
    for (const annotation::BrushSet& set : m_brushSets) {
        for (const annotation::BrushTip& tip : set.tips) {
            if (tip.id != m_settings.brushTipId) continue;
            m_brushButton->setToolTip(tr("Choose brush (now: %1), or import Photoshop brushes (.abr)")
                                          .arg(tip.name.isEmpty() ? tr("Brush") : tip.name));
            return;
        }
    }
    m_brushButton->setToolTip(m_brushSets.isEmpty() ? tr("Import Photoshop brushes (.abr) for the Custom tool")
                                                    : tr("Choose an imported brush, or import Photoshop brushes (.abr)"));
}

QList<QColor> AnnotationToolbar::presetColors()
{
    return {QColor(230, 30, 30), QColor(255, 140, 0), QColor(250, 215, 40), QColor(40, 200, 80),
            QColor(40, 170, 255), QColor(150, 80, 230), QColor(255, 255, 255), QColor(20, 20, 20)};
}

void AnnotationToolbar::addToolAction(AnnotationTool tool, const QString& iconName, const QString& label,
                                      const QString& tip)
{
    QAction* action = addAction(icons::icon(iconName), label);
    action->setCheckable(true);
    action->setToolTip(tip);
    m_toolGroup->addAction(action);
    m_toolActions.insert(int(tool), action);
    connect(action, &QAction::triggered, this, [this, tool] {
        if (m_updating) return;
        auto next = m_settings;
        next.tool = tool;
        apply(next, true);
    });
}

QToolButton* AnnotationToolbar::addIconButton(QToolButton* button, const QString& iconName)
{
    button->setIcon(icons::icon(iconName));
    button->setIconSize(iconSize());
    button->setAccessibleName(button->text().isEmpty() ? button->toolTip() : button->text());
    addWidget(button);
    return button;
}

QList<QWidget*> AnnotationToolbar::viewTools() const
{
    return {m_traceButton, m_ghostButton};
}

void AnnotationToolbar::attachSizePill(BrushSizePill* pill)
{
    m_pill = pill;
    if (!pill) return;
    // The size controls edit the eraser's size while the Eraser is selected.
    connect(pill, &BrushSizePill::sizeChanged, this, [this](qreal pixels) {
        auto next = m_settings;
        next.setActiveSizePixels(pixels);
        apply(next, true);
    });
    connect(pill, &BrushSizePill::opacityChanged, this, [this](qreal opacity) {
        auto next = m_settings;
        next.opacity = opacity;
        apply(next, true);
    });
    pill->setValues(m_settings.activeSizePixels(), m_settings.opacity, m_settings.tool == AnnotationTool::Eraser);
}

void AnnotationToolbar::addEditActions(const QList<QAction*>& actions)
{
    // colour | undo, redo, clear | tool options
    insertSeparator(m_editAnchor);
    insertActions(m_editAnchor, actions);
}

void AnnotationToolbar::setSettings(const annotation::AnnotationToolSettings& settings)
{
    apply(settings, false);
}

void AnnotationToolbar::apply(const annotation::AnnotationToolSettings& settings, bool notify)
{
    m_settings = settings;
    m_settings.sizePixels = std::clamp(m_settings.sizePixels, annotation::AnnotationToolSettings::kMinSizePixels,
                                       annotation::AnnotationToolSettings::kMaxSizePixels);
    m_settings.eraserSizePixels = std::clamp(m_settings.eraserSizePixels,
                                             annotation::AnnotationToolSettings::kMinSizePixels,
                                             annotation::AnnotationToolSettings::kMaxSizePixels);
    m_updating = true;
    if (QAction* action = m_toolActions.value(int(m_settings.tool))) action->setChecked(true);
    if (m_pill)
        m_pill->setValues(m_settings.activeSizePixels(), m_settings.opacity, m_settings.tool == AnnotationTool::Eraser);
    m_eraserModeCombo->setCurrentIndex(m_eraserModeCombo->findData(int(m_settings.eraserMode)));
    m_pressureCheck->setChecked(m_settings.usePressure);
    m_curveCombo->setCurrentIndex(m_curveCombo->findData(int(m_settings.pressureCurve)));
    m_curveCombo->setEnabled(m_settings.usePressure);
    m_stabilizerSpin->setValue(qRound(m_settings.stabilizer * 100.0));
    refreshColorButton();
    refreshBrushButton();
    m_updating = false;
    if (notify) emit settingsChanged(m_settings);
}

void AnnotationToolbar::refreshColorButton()
{
    m_colorButton->setIcon(swatchIcon(m_settings.color, theme::metrics::iconLarge));
    m_colorButton->setText(m_settings.color.name());
    m_colorButton->setToolTip(tr("Colour: %1").arg(m_settings.color.name()));
}

void AnnotationToolbar::chooseCustomColor()
{
    const QColor chosen = QColorDialog::getColor(m_settings.color, this, tr("Drawing Colour"));
    if (!chosen.isValid()) return;
    auto next = m_settings;
    next.color = chosen;
    apply(next, true);
}

} // namespace atk::ui
