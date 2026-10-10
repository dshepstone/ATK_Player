#include "ui/AnnotationToolbar.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

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
    addToolAction(AnnotationTool::Pencil, tr("Pencil"), tr("Pencil: grainy, pressure sets width and darkness"));
    addToolAction(AnnotationTool::Pen, tr("Pen"), tr("Pen: smooth ink, pressure sets width"));
    addToolAction(AnnotationTool::Brush, tr("Brush"), tr("Brush: soft edge, pressure sets width and opacity"));
    addToolAction(AnnotationTool::Marker, tr("Marker"), tr("Marker: flat, even, translucent"));
    addToolAction(AnnotationTool::Eraser, tr("Eraser"), tr("Eraser: removes whole strokes (the pen's eraser end works too)"));
    addSeparator();
    addToolAction(AnnotationTool::Line, tr("Line"), tr("Straight line"));
    addToolAction(AnnotationTool::Arrow, tr("Arrow"), tr("Arrow"));
    addToolAction(AnnotationTool::Rectangle, tr("Rect"), tr("Rectangle"));
    addToolAction(AnnotationTool::Ellipse, tr("Ellipse"), tr("Ellipse"));
    addToolAction(AnnotationTool::Text, tr("Text"), tr("Text label: click where it should go"));
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
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;
        auto next = m_settings;
        next.sizePixels = pixelsFromSlider(value);
        apply(next, true);
    });
    connect(m_sizeSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating) return;
        auto next = m_settings;
        next.sizePixels = value;
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

    apply(m_settings, false);
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
    m_updating = true;
    if (QAction* action = m_toolActions.value(int(m_settings.tool))) action->setChecked(true);
    m_sizeSlider->setValue(sliderFromPixels(m_settings.sizePixels));
    m_sizeSpin->setValue(qRound(m_settings.sizePixels));
    m_opacitySpin->setValue(qRound(m_settings.opacity * 100.0));
    m_pressureCheck->setChecked(m_settings.usePressure);
    m_curveCombo->setCurrentIndex(m_curveCombo->findData(int(m_settings.pressureCurve)));
    m_curveCombo->setEnabled(m_settings.usePressure);
    m_stabilizerSpin->setValue(qRound(m_settings.stabilizer * 100.0));
    refreshColorButton();
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
