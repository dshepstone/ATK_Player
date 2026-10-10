#pragma once

#include "annotation/AnnotationEraser.h"
#include "annotation/AnnotationTypes.h"
#include "annotation/StrokeBuilder.h"

#include <QColor>

namespace atk::annotation {

/// The reviewer's current drawing tool and its options. Plain data shared by
/// the toolbar (which edits it), the viewer (which draws with it) and
/// settings persistence.
///
/// `sizePixels` is the brush diameter in *source image* pixels, so "8 px" means
/// the same thing whatever the window size or zoom; it becomes the stored,
/// resolution-independent AnnotationItem::size when a stroke is committed.
struct AnnotationToolSettings {
    static constexpr qreal kMinSizePixels = 1.0;
    static constexpr qreal kMaxSizePixels = 200.0;

    AnnotationTool tool = AnnotationTool::Pen;
    QColor color = QColor(230, 30, 30);
    qreal sizePixels = 6.0;
    qreal opacity = 1.0;
    bool usePressure = true;
    PressureCurve pressureCurve = PressureCurve::Linear;
    qreal stabilizer = 0.0; // 0..1
    /// The eraser keeps its own size, so switching between pen and eraser (or
    /// flipping the stylus) does not leave a pen-sized eraser.
    qreal eraserSizePixels = 24.0;
    EraserMode eraserMode = EraserMode::Area;
    /// Imported brush tip used by the Stamp ("Custom") tool.
    QString brushTipId;

    /// Size the size controls and [ ] keys act on: the eraser's while the
    /// Eraser tool is selected, the brush's otherwise.
    qreal activeSizePixels() const { return tool == AnnotationTool::Eraser ? eraserSizePixels : sizePixels; }
    void setActiveSizePixels(qreal pixels);

    /// Eraser radius in image pixels for a pen `pressure` (mouse = 1.0). Light
    /// pressure gives a smaller eraser, as on paper.
    qreal eraserRadius(float pressure) const;

    /// Item size for a picture `imageHeight` source pixels tall, clamped to the
    /// stored limits.
    qreal itemSize(qreal imageHeight) const;

    /// A new item of the current tool and look, without points.
    AnnotationItem makeItem(qreal imageHeight) const;

    friend bool operator==(const AnnotationToolSettings&, const AnnotationToolSettings&) = default;
};

/// Steps a brush size up or down by a perceptually even amount (about 15%),
/// clamped to the allowed range. Used by the [ and ] keys.
qreal stepBrushSize(qreal sizePixels, int direction);

} // namespace atk::annotation
