#include "ui/FrameFieldStyle.h"
#include "ui/Theme.h"

#include <QAbstractSpinBox>
#include <QFontMetrics>
#include <QSpinBox>

#include <algorithm>

namespace atk::ui {
namespace {

constexpr int kMinimumFieldWidth = 64;
constexpr int kFieldHorizontalChrome = 40;

} // namespace

void configureFrameField(QSpinBox* field)
{
    field->setAlignment(Qt::AlignCenter);
    field->setButtonSymbols(QAbstractSpinBox::NoButtons);
    field->setFixedHeight(26);
    field->setStyleSheet(QStringLiteral(R"CSS(
        QSpinBox {
            background-color: %1;
            color: %2;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 2px 10px;
            selection-background-color: %3;
        }
        QSpinBox:hover {
            background-color: %4;
            border-color: %5;
        }
        QSpinBox:focus {
            background-color: %4;
            border-color: %2;
        }
        QSpinBox:disabled {
            background-color: %6;
            color: %7;
            border-color: transparent;
        }
    )CSS")
        .arg(theme::controlBackground().name(), theme::accent().name(),
             theme::accentMuted().name(), theme::controlHover().name(),
             theme::panelBorder().name(), theme::panelBackground().name(),
             theme::textDisabled().name()));
}

void updateFrameFieldWidth(QSpinBox* field, int maximumVisibleFrame)
{
    const QString maximumText = QString::number(std::max(1, maximumVisibleFrame));
    const int measuredWidth = QFontMetrics(field->font()).horizontalAdvance(maximumText)
        + kFieldHorizontalChrome;
    field->setFixedWidth(std::max(kMinimumFieldWidth, measuredWidth));
}

} // namespace atk::ui
