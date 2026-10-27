#pragma once

class QSpinBox;

namespace atk::ui {

/// Applies the shared compact, buttonless appearance used by timeline frame fields.
void configureFrameField(QSpinBox* field);

/// Restyles a configured field as the large, yellow current-frame readout
/// beside the timeline. Call after configureFrameField().
void configureLargeFrameField(QSpinBox* field);

/// Sizes a configured field for the largest relevant one-based frame label.
void updateFrameFieldWidth(QSpinBox* field, int maximumVisibleFrame);

} // namespace atk::ui
