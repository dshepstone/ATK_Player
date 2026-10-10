#include "annotation/AnnotationToolSettings.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace atk::annotation {

qreal AnnotationToolSettings::itemSize(qreal imageHeight) const
{
    if (!(imageHeight > 0.0)) imageHeight = 1080.0;
    const qreal pixels = std::clamp(sizePixels, kMinSizePixels, kMaxSizePixels);
    return std::clamp(pixels / imageHeight, AnnotationLimits::kMinSize, AnnotationLimits::kMaxSize);
}

AnnotationItem AnnotationToolSettings::makeItem(qreal imageHeight) const
{
    AnnotationItem item;
    item.tool = tool;
    item.color = color.isValid() ? color : QColor(230, 30, 30);
    item.size = itemSize(imageHeight);
    item.opacity = std::clamp(opacity, 0.05, 1.0);
    item.textureSeed = QRandomGenerator::global()->generate();
    return item;
}

qreal stepBrushSize(qreal sizePixels, int direction)
{
    const qreal current = std::clamp(sizePixels, AnnotationToolSettings::kMinSizePixels,
                                     AnnotationToolSettings::kMaxSizePixels);
    qreal next = direction > 0 ? current * 1.15 : current / 1.15;
    // Always move by at least one pixel so small sizes do not get stuck.
    if (direction > 0) next = std::max(next, current + 1.0);
    else if (direction < 0) next = std::min(next, current - 1.0);
    return std::clamp(std::round(next * 2.0) / 2.0, AnnotationToolSettings::kMinSizePixels,
                      AnnotationToolSettings::kMaxSizePixels);
}

} // namespace atk::annotation
