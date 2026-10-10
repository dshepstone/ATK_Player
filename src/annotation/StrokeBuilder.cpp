#include "annotation/StrokeBuilder.h"

#include <algorithm>
#include <cmath>

namespace atk::annotation {

float applyPressureCurve(float pressure, PressureCurve curve)
{
    if (!std::isfinite(pressure) || pressure <= 0.0f) return kMinimumPressure;
    pressure = std::min(pressure, 1.0f);
    float shaped = pressure;
    switch (curve) {
    case PressureCurve::Soft: shaped = std::pow(pressure, 0.6f); break;
    case PressureCurve::Linear: break;
    case PressureCurve::Firm: shaped = std::pow(pressure, 1.7f); break;
    }
    return std::clamp(shaped, kMinimumPressure, 1.0f);
}

StrokeBuilder::StrokeBuilder(Settings settings, const QSizeF& imageSize)
    : m_settings(settings), m_imageSize(imageSize)
{
    m_settings.stabilizer = std::clamp<qreal>(m_settings.stabilizer, 0.0, 0.95);
    if (m_imageSize.width() <= 0 || m_imageSize.height() <= 0) m_imageSize = QSizeF(1920, 1080);
}

bool StrokeBuilder::addSample(const QPointF& normalized, float rawPressure, float tiltX, float tiltY)
{
    if (!std::isfinite(normalized.x()) || !std::isfinite(normalized.y())) return false;
    const QPointF clamped(std::clamp<qreal>(normalized.x(), 0.0, 1.0),
                          std::clamp<qreal>(normalized.y(), 0.0, 1.0));
    if (!m_started) {
        m_smoothed = clamped;
        m_started = true;
    } else {
        // Lag-follow: the higher the stabiliser, the less each sample moves the pen.
        m_smoothed += (clamped - m_smoothed) * (1.0 - m_settings.stabilizer);
    }
    StrokePoint point;
    point.x = float(m_smoothed.x());
    point.y = float(m_smoothed.y());
    point.pressure = m_settings.usePressure ? applyPressureCurve(rawPressure, m_settings.curve) : 1.0f;
    point.tiltX = std::isfinite(tiltX) ? tiltX : 0.0f;
    point.tiltY = std::isfinite(tiltY) ? tiltY : 0.0f;
    m_lastRaw = point;
    m_lastRaw.x = float(clamped.x());
    m_lastRaw.y = float(clamped.y());
    return store(point, m_points.isEmpty());
}

bool StrokeBuilder::store(const StrokePoint& point, bool force)
{
    if (!force && !m_points.isEmpty()) {
        const StrokePoint& last = m_points.constLast();
        const qreal dx = (qreal(point.x) - last.x) * m_imageSize.width();
        const qreal dy = (qreal(point.y) - last.y) * m_imageSize.height();
        if (std::hypot(dx, dy) < m_settings.minimumSpacing) return false;
    }
    if (m_points.size() >= AnnotationLimits::kMaxPointsPerItem) return false;
    m_points.append(point);
    return true;
}

QVector<StrokePoint> StrokeBuilder::finish()
{
    if (m_started && !m_points.isEmpty()) {
        const StrokePoint& last = m_points.constLast();
        if (last.x != m_lastRaw.x || last.y != m_lastRaw.y) store(m_lastRaw, true);
    }
    return m_points;
}

} // namespace atk::annotation
