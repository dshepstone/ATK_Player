#include "ui/BrushSizePill.h"

#include "annotation/AnnotationToolSettings.h"
#include "ui/Theme.h"

#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace atk::ui {
namespace {

using annotation::AnnotationToolSettings;

// The size slider is logarithmic: fine control for small pens, still reaching
// a 200 px marker.
constexpr int kSliderSteps = 1000;

int sliderFromPixels(qreal pixels)
{
    const qreal minimum = std::log(AnnotationToolSettings::kMinSizePixels);
    const qreal maximum = std::log(AnnotationToolSettings::kMaxSizePixels);
    return qRound((std::log(std::max<qreal>(pixels, 1.0)) - minimum) / (maximum - minimum) * kSliderSteps);
}

qreal pixelsFromSlider(int value)
{
    const qreal minimum = std::log(AnnotationToolSettings::kMinSizePixels);
    const qreal maximum = std::log(AnnotationToolSettings::kMaxSizePixels);
    return std::round(std::exp(minimum + (maximum - minimum) * value / kSliderSteps));
}

QSpinBox* makeField(QWidget* parent, int minimum, int maximum, const QString& objectName)
{
    auto* field = new QSpinBox(parent);
    field->setObjectName(objectName);
    field->setRange(minimum, maximum);
    field->setButtonSymbols(QAbstractSpinBox::NoButtons);
    field->setAlignment(Qt::AlignCenter);
    field->setFixedWidth(40);
    return field;
}

QSlider* makeSlider(QWidget* parent, int minimum, int maximum, const QString& objectName)
{
    auto* slider = new QSlider(Qt::Vertical, parent);
    slider->setObjectName(objectName);
    slider->setRange(minimum, maximum);
    slider->setFixedHeight(130);
    slider->setFocusPolicy(Qt::NoFocus);
    return slider;
}

} // namespace

BrushSizePill::BrushSizePill(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("BrushSizePill"));
    setStyleSheet(QStringLiteral(R"CSS(
QFrame#BrushSizePill {
    background-color: %1;
    border: 1px solid %2;
    border-radius: %3px;
}
QFrame#BrushSizePill QLabel { background: transparent; color: %4; font-size: 10px; }
QFrame#BrushSizePill QSpinBox {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    font-weight: 600;
}
QFrame#BrushSizePill QSpinBox:hover, QFrame#BrushSizePill QSpinBox:focus { border-color: %2; }
)CSS")
                      .arg(theme::panelBackground().name(), theme::controlHover().name())
                      .arg(theme::metrics::radiusMedium + 4)
                      .arg(theme::textSecondary().name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 8, 4, 8);
    layout->setSpacing(4);

    m_sizeField = makeField(this, int(AnnotationToolSettings::kMinSizePixels),
                            int(AnnotationToolSettings::kMaxSizePixels), QStringLiteral("BrushSizeField"));
    m_sizeSlider = makeSlider(this, 0, kSliderSteps, QStringLiteral("BrushSizeSlider"));
    m_sizeCaption = new QLabel(tr("px"), this);
    m_opacityField = makeField(this, 5, 100, QStringLiteral("BrushOpacityField"));
    m_opacitySlider = makeSlider(this, 5, 100, QStringLiteral("BrushOpacitySlider"));
    auto* opacityCaption = new QLabel(tr("opacity"), this);

    layout->addWidget(m_sizeField, 0, Qt::AlignHCenter);
    layout->addWidget(m_sizeCaption, 0, Qt::AlignHCenter);
    layout->addWidget(m_sizeSlider, 0, Qt::AlignHCenter);
    layout->addSpacing(theme::metrics::spacingMedium);
    layout->addWidget(m_opacityField, 0, Qt::AlignHCenter);
    layout->addWidget(opacityCaption, 0, Qt::AlignHCenter);
    layout->addWidget(m_opacitySlider, 0, Qt::AlignHCenter);

    m_opacityField->setSuffix(QStringLiteral("%"));
    m_opacityField->setAccessibleName(tr("Opacity"));
    m_opacitySlider->setAccessibleName(tr("Opacity"));
    m_opacitySlider->setToolTip(tr("Opacity"));
    m_opacityField->setToolTip(tr("Opacity"));

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;
        const qreal pixels = pixelsFromSlider(value);
        {
            QSignalBlocker blocker(m_sizeField);
            m_sizeField->setValue(qRound(pixels));
        }
        emit sizeChanged(pixels);
    });
    connect(m_sizeField, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating) return;
        {
            QSignalBlocker blocker(m_sizeSlider);
            m_sizeSlider->setValue(sliderFromPixels(value));
        }
        emit sizeChanged(value);
    });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;
        {
            QSignalBlocker blocker(m_opacityField);
            m_opacityField->setValue(value);
        }
        emit opacityChanged(value / 100.0);
    });
    connect(m_opacityField, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating) return;
        {
            QSignalBlocker blocker(m_opacitySlider);
            m_opacitySlider->setValue(value);
        }
        emit opacityChanged(value / 100.0);
    });

    setValues(AnnotationToolSettings{}.sizePixels, AnnotationToolSettings{}.opacity, false);
    adjustSize();
}

void BrushSizePill::setValues(qreal sizePixels, qreal opacity, bool eraser)
{
    m_updating = true;
    m_sizeSlider->setValue(sliderFromPixels(sizePixels));
    m_sizeField->setValue(qRound(sizePixels));
    m_opacitySlider->setValue(qRound(opacity * 100.0));
    m_opacityField->setValue(qRound(opacity * 100.0));
    // Opacity does not apply to the eraser.
    m_opacitySlider->setEnabled(!eraser);
    m_opacityField->setEnabled(!eraser);
    const QString sizeTip = eraser ? tr("Eraser size ([ and ] to change)") : tr("Brush size ([ and ] to change)");
    for (QWidget* w : {static_cast<QWidget*>(m_sizeSlider), static_cast<QWidget*>(m_sizeField)}) {
        w->setToolTip(sizeTip);
        w->setAccessibleName(eraser ? tr("Eraser size") : tr("Brush size"));
    }
    m_sizeCaption->setText(eraser ? tr("eraser px") : tr("px"));
    m_updating = false;
}

qreal BrushSizePill::sizePixels() const
{
    return m_sizeField->value();
}

qreal BrushSizePill::opacity() const
{
    return m_opacityField->value() / 100.0;
}

} // namespace atk::ui
