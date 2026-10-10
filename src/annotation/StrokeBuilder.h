#pragma once

#include "annotation/AnnotationTypes.h"

#include <QPointF>
#include <QSizeF>

namespace atk::annotation {

/// How tablet pressure maps to stroke pressure. Applied once, at capture, so
/// a preference change never alters drawings already made -- and the viewer
/// and exports always agree.
enum class PressureCurve { Soft, Linear, Firm };

/// Maps raw device pressure (0..1) through `curve`. A non-zero touch never
/// maps below `kMinimumPressure`, so a light touch still marks.
float applyPressureCurve(float pressure, PressureCurve curve);
constexpr float kMinimumPressure = 0.05f;

/// Turns raw pointer samples into stored stroke points.
///
/// Pure and widget-free: the viewer feeds it mouse or tablet samples already
/// mapped to normalised image coordinates, and tests feed it synthetic ones.
///
///   - Pressure goes through the selected curve.
///   - The stabiliser (0 = off .. 1 = heavy) pulls the pen toward the pointer
///     with lag, which smooths shaky mouse lines.
///   - Samples closer than `minimumSpacing` (in image pixels) to the previous
///     stored point are dropped, so a resting pen does not pile up points.
///   - finish() adds the true final position, so stabiliser lag never cuts the
///     end of a line short.
class StrokeBuilder {
public:
    struct Settings {
        PressureCurve curve = PressureCurve::Linear;
        bool usePressure = true;
        qreal stabilizer = 0.0;
        qreal minimumSpacing = 1.0; // image pixels
    };

    /// `imageSize` is the source picture's pixel size; it converts spacing to
    /// normalised units.
    StrokeBuilder(Settings settings, const QSizeF& imageSize);

    /// Adds a sample in normalised image coordinates. Points outside 0..1
    /// are clamped onto the image edge. Returns true if a point was stored.
    bool addSample(const QPointF& normalized, float rawPressure, float tiltX = 0.0f, float tiltY = 0.0f);

    /// Completes the stroke and returns its points.
    QVector<StrokePoint> finish();

    const QVector<StrokePoint>& points() const { return m_points; }
    bool isEmpty() const { return m_points.isEmpty(); }

private:
    bool store(const StrokePoint& point, bool force);

    Settings m_settings;
    QSizeF m_imageSize;
    QVector<StrokePoint> m_points;
    QPointF m_smoothed;
    bool m_started = false;
    StrokePoint m_lastRaw;
};

} // namespace atk::annotation
