#pragma once

#include <QColor>
#include <QString>

namespace atk::ui::theme {

// ---------------------------------------------------------------------------
// A neutral dark palette.
//
// Review players are dark for a practical reason rather than a stylistic one:
// a bright interface around the viewer changes how the reviewer perceives the
// values in the image. Everything outside the picture stays low-contrast and
// desaturated so the picture is the only thing competing for attention.
//
// Colours are defined once here; widgets never hard-code a hex value.
// ---------------------------------------------------------------------------

inline QColor windowBackground()  { return QColor(0x21, 0x23, 0x26); }
inline QColor panelBackground()   { return QColor(0x2A, 0x2C, 0x30); }
inline QColor panelBorder()       { return QColor(0x17, 0x18, 0x1A); }
inline QColor controlBackground() { return QColor(0x34, 0x37, 0x3C); }
inline QColor controlHover()      { return QColor(0x3E, 0x42, 0x48); }
inline QColor controlPressed()    { return QColor(0x4A, 0x4F, 0x56); }

/// Pure black behind the picture, so nothing tints the image being reviewed.
inline QColor viewerBackground()  { return QColor(0x00, 0x00, 0x00); }

inline QColor textPrimary()       { return QColor(0xDA, 0xDD, 0xE1); }
inline QColor textSecondary()     { return QColor(0x8B, 0x92, 0x9B); }
inline QColor textDisabled()      { return QColor(0x5C, 0x62, 0x69); }

inline QColor accent()            { return QColor(0x4C, 0x9E, 0xE0); }
inline QColor accentMuted()       { return QColor(0x35, 0x6E, 0x9C); }

inline QColor timelineTrack()     { return QColor(0x1B, 0x1D, 0x20); }
inline QColor timelineFill()      { return QColor(0x3A, 0x3E, 0x44); }
inline QColor timelineRange()     { return QColor(0x35, 0x5A, 0x77); }
inline QColor playhead()          { return QColor(0xE8, 0x9A, 0x3C); }
inline QColor tickMark()          { return QColor(0x54, 0x5A, 0x62); }

/// Application-wide stylesheet built from the palette above.
QString styleSheet();

} // namespace atk::ui::theme
