#include "ui/AnnotationToolbar.h"

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
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QWidgetAction>

#include <cmath>

namespace atk::ui {
namespace {
using annotation::AnnotationTool;

QIcon swatchIcon(const QColor& color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
    return QIcon(pixmap);
}

// The size slider is logarithmic: fine control for small pens, still reaching
// a 200 px marker.
constexpr int kSliderSteps = 1000;
int sliderFromPixels(qreal pixels)
{
    const qreal minimum = std::log(annotation::AnnotationToolSettings::kMinSizePixels);
    const qreal maximum = std::log(annotation::AnnotationToolSettings::kMaxSizePixels);
    return qRound((std::log(std::max<qreal>(pixels, 1.0)) - minimum) / (maximum - minimum) * kSliderSteps);
}
qreal pixelsFromSlider(int value)
{
    const qreal minimum = std::log(annotation::AnnotationToolSettings::kMinSizePixels);
    const qreal maximum = std::log(annotation::AnnotationToolSettings::kMaxSizePixels);
    return std::round(std::exp(minimum + (maximum - minimum) * value / kSliderSteps));
}
} // namespace

AnnotationToolbar::AnnotationToolbar(QWidget* parent) : QToolBar(tr("Drawing"), parent)
{
    setObjectName(QStringLiteral("annotationToolbar"));
    setMovable(true);
    setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    addToolAction(AnnotationTool::Select, tr("Select"),
                  tr("Select: click a drawing or text to select it, drag to move it, Delete to remove it, "
                     "double-click text to edit it"));
    addSeparator();
    addToolAction(AnnotationTool::Pencil, tr("Pencil"), tr("Pencil: grainy, pressure sets width and darkness"));
    addToolAction(AnnotationTool::Pen, tr("Pen"), tr("Pen: smooth ink, pressure sets width"));
    addToolAction(AnnotationTool::Brush, tr("Brush"), tr("Brush: soft edge, pressure sets width and opacity"));
    addToolAction(AnnotationTool::Marker, tr("Marker"), tr("Marker: flat, even, translucent"));
    addToolAction(AnnotationTool::Stamp, tr("Custom"),
                  tr("Custom brush: paints with an imported brush tip (for example from a Photoshop .abr set)"));
    m_brushButton = new QToolButton(this);
    m_brushButton->setPopupMode(QToolButton::InstantPopup);
    m_brushButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_brushButton->setToolTip(tr("Choose an imported brush, or import Photoshop brushes (.abr)"));
    m_brushButton->setMenu(new QMenu(m_brushButton));
    addWidget(m_brushButton);
    addToolAction(AnnotationTool::Eraser, tr("Eraser"), tr("Eraser (the pen's eraser end works too)"));
    m_eraserModeCombo = new QComboBox(this);
    m_eraserModeCombo->addItem(tr("Erase area"), int(annotation::EraserMode::Area));
    m_eraserModeCombo->addItem(tr("Erase stroke"), int(annotation::EraserMode::WholeStroke));
    m_eraserModeCombo->setToolTip(tr("Erase area removes only the ink under the eraser; "
                                     "Erase stroke removes every stroke it touches"));
    addWidget(m_eraserModeCombo);
    connect(m_eraserModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0) return;
        auto next = m_settings;
        next.eraserMode = annotation::EraserMode(m_eraserModeCombo->itemData(index).toInt());
        apply(next, true);
    });
    addSeparator();
    addToolAction(AnnotationTool::Line, tr("Line"), tr("Straight line"));
    addToolAction(AnnotationTool::Arrow, tr("Arrow"), tr("Arrow"));
    addToolAction(AnnotationTool::Rectangle, tr("Rect"), tr("Rectangle"));
    addToolAction(AnnotationTool::Ellipse, tr("Ellipse"), tr("Ellipse"));
    addToolAction(AnnotationTool::Text, tr("Text"), tr("Text: click to type at a point, or drag a box for text that wraps. "
                                                   "Ctrl+Enter or click away to finish, Esc to cancel"));
    addSeparator();

    m_colorButton = new QToolButton(this);
    m_colorButton->setToolTip(tr("Colour"));
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
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

    addWidget(new QLabel(tr(" Size "), this));
    m_sizeSlider = new QSlider(Qt::Horizontal, this);
    m_sizeSlider->setRange(0, kSliderSteps);
    m_sizeSlider->setFixedWidth(110);
    m_sizeSlider->setToolTip(tr("Brush size ([ and ] to change)"));
    addWidget(m_sizeSlider);
    m_sizeSpin = new QSpinBox(this);
    m_sizeSpin->setRange(int(annotation::AnnotationToolSettings::kMinSizePixels),
                         int(annotation::AnnotationToolSettings::kMaxSizePixels));
    m_sizeSpin->setSuffix(tr(" px"));
    m_sizeSpin->setToolTip(tr("Brush size in source-image pixels"));
    addWidget(m_sizeSpin);
    // The size controls edit the eraser's size while the Eraser is selected.
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;
        auto next = m_settings;
        next.setActiveSizePixels(pixelsFromSlider(value));
        apply(next, true);
    });
    connect(m_sizeSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating) return;
        auto next = m_settings;
        next.setActiveSizePixels(value);
        apply(next, true);
    });

    addWidget(new QLabel(tr(" Opacity "), this));
    m_opacitySpin = new QSpinBox(this);
    m_opacitySpin->setRange(5, 100);
    m_opacitySpin->setSuffix(QStringLiteral("%"));
    addWidget(m_opacitySpin);
    connect(m_opacitySpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating) return;
        auto next = m_settings;
        next.opacity = value / 100.0;
        apply(next, true);
    });
    addSeparator();

    m_pressureCheck = new QCheckBox(tr("Pressure"), this);
    m_pressureCheck->setToolTip(tr("Use pen pressure from a drawing tablet"));
    addWidget(m_pressureCheck);
    connect(m_pressureCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        auto next = m_settings;
        next.usePressure = on;
        apply(next, true);
    });
    m_curveCombo = new QComboBox(this);
    m_curveCombo->addItem(tr("Soft"), int(annotation::PressureCurve::Soft));
    m_curveCombo->addItem(tr("Linear"), int(annotation::PressureCurve::Linear));
    m_curveCombo->addItem(tr("Firm"), int(annotation::PressureCurve::Firm));
    m_curveCombo->setToolTip(tr("Pressure response: Soft needs a lighter touch, Firm a heavier one"));
    addWidget(m_curveCombo);
    connect(m_curveCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0) return;
        auto next = m_settings;
        next.pressureCurve = annotation::PressureCurve(m_curveCombo->itemData(index).toInt());
        apply(next, true);
    });

    addWidget(new QLabel(tr(" Stabiliser "), this));
    m_stabilizerSpin = new QSpinBox(this);
    m_stabilizerSpin->setRange(0, 95);
    m_stabilizerSpin->setSuffix(QStringLiteral("%"));
    m_stabilizerSpin->setToolTip(tr("Smooths shaky lines; higher values lag behind the pointer more"));
    addWidget(m_stabilizerSpin);
    connect(m_stabilizerSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating) return;
        auto next = m_settings;
        next.stabilizer = value / 100.0;
        apply(next, true);
    });

    addSeparator();

    // Tracing paper: a white wash under the drawings, like SyncSketch's.
    m_traceButton = new QToolButton(this);
    m_traceButton->setObjectName(QStringLiteral("TracingPaperButton"));
    m_traceButton->setText(tr("Trace"));
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
    addWidget(m_traceButton);
    connect(m_traceButton, &QToolButton::clicked, this,
            [this](bool on) { emit tracingPaperRequested(on ? m_traceLevel : 0); });

    // Ghosting: drawings from nearby frames, faintly (onion skin).
    m_ghostButton = new QToolButton(this);
    m_ghostButton->setObjectName(QStringLiteral("GhostButton"));
    m_ghostButton->setText(tr("Ghost"));
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
    addWidget(m_ghostButton);
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
            QImage preview = tip.mask.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            preview.invertPixels();
            m_brushButton->setIcon(QIcon(QPixmap::fromImage(preview)));
            m_brushButton->setText(tip.name.left(18));
            return;
        }
    }
    m_brushButton->setIcon({});
    m_brushButton->setText(m_brushSets.isEmpty() ? tr("Brushes") : tr("Choose brush"));
}

QList<QColor> AnnotationToolbar::presetColors()
{
    return {QColor(230, 30, 30), QColor(255, 140, 0), QColor(250, 215, 40), QColor(40, 200, 80),
            QColor(40, 170, 255), QColor(150, 80, 230), QColor(255, 255, 255), QColor(20, 20, 20)};
}

void AnnotationToolbar::addToolAction(AnnotationTool tool, const QString& label, const QString& tip)
{
    QAction* action = addAction(label);
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
    m_sizeSlider->setValue(sliderFromPixels(m_settings.activeSizePixels()));
    m_sizeSpin->setValue(qRound(m_settings.activeSizePixels()));
    const QString sizeTip = m_settings.tool == AnnotationTool::Eraser ? tr("Eraser size ([ and ] to change)")
                                                                      : tr("Brush size ([ and ] to change)");
    m_sizeSlider->setToolTip(sizeTip);
    m_sizeSpin->setToolTip(sizeTip);
    m_eraserModeCombo->setCurrentIndex(m_eraserModeCombo->findData(int(m_settings.eraserMode)));
    m_opacitySpin->setValue(qRound(m_settings.opacity * 100.0));
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
    m_colorButton->setIcon(swatchIcon(m_settings.color));
    m_colorButton->setText(m_settings.color.name());
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
