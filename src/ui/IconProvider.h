#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace atk::ui::icons {

// ---------------------------------------------------------------------------
// Monochrome interface icons, tinted from the theme at draw time.
//
// The icons are Lucide outlines (ISC licence) compiled into the UI library
// under :/icons/lucide. Each SVG strokes with `currentColor`; the provider
// substitutes a palette colour and rasterises at the exact device size Qt
// asks for, so one file serves every state and every scale factor.
//
// GUI thread only: rasterising goes through QPixmap and a shared cache.
// ---------------------------------------------------------------------------

/// Colours a themed icon uses for each QIcon mode and state.
struct IconColors {
    QColor normal;
    QColor hover;    // QIcon::Active
    QColor selected; // QIcon::Selected
    QColor disabled;
    QColor checked;  // QIcon::On, drawn on an accent-filled button
};

/// The interface default: muted outline, brighter on hover, light on the
/// accent fill when checked, dim when disabled.
IconColors defaultColors();

/// True when `name` (without ".svg") is one of the bundled icons.
bool exists(const QString& name);

/// A themed icon whose colour follows its mode and state. An unknown name
/// returns a null QIcon and logs a warning, rather than drawing nothing
/// silently.
QIcon icon(const QString& name);
QIcon icon(const QString& name, const IconColors& colors);

/// The same icon in a single colour for every mode and state.
QIcon icon(const QString& name, const QColor& color);

/// One tinted rendering at `size` logical pixels for a display with the given
/// device pixel ratio. Null for an unknown name.
QPixmap pixmap(const QString& name, const QSize& size, const QColor& color, qreal devicePixelRatio = 1.0);

} // namespace atk::ui::icons
