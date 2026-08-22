#pragma once

#include "playback/CompareSession.h"
#include <QImage>

namespace atk::playback {

/// Pure, widget-free comparison rendering shared by the viewer and exporter.
class ComparisonCompositor final {
public:
    static QSize canvasSize(const QSize& sourceASize, CompareLayout mode);
    static QImage fitToCanvas(const QImage& image, const QSize& canvas);
    static QImage compose(const QImage& a, const QImage& b,
                          CompareLayout mode, int amount);
};

} // namespace atk::playback
