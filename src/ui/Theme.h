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

/// The one colour that means "active", "selected" or "primary". Teal, in the
/// spirit of the review tools reviewers already know; change it here and the
/// whole interface follows. 6:1 against the window background.
inline QColor accent()            { return QColor(0x1F, 0xB5, 0x8F); }
/// Filled accent tiles (the checked tool, primary buttons): dark enough that
/// textOnAccent() reads at 4.3:1 on it.
inline QColor accentFill()        { return QColor(0x14, 0x86, 0x6A); }
inline QColor accentFillHover()   { return QColor(0x18, 0x97, 0x78); }
/// Quiet accent for selection backgrounds (list rows, menus).
inline QColor accentMuted()       { return QColor(0x1E, 0x5E, 0x4F); }
inline QColor textOnAccent()      { return QColor(0xF4, 0xFB, 0xF8); }

// Icons: monochrome outlines tinted from these, never baked-in colours.
inline QColor iconNormal()        { return QColor(0xB4, 0xBA, 0xC2); }
inline QColor iconHover()         { return textPrimary(); }
inline QColor iconDisabled()      { return textDisabled(); }

// Cards (playlist items, review rows) and the small badges on them.
inline QColor cardBackground()    { return QColor(0x2F, 0x32, 0x36); }
inline QColor cardHover()         { return QColor(0x37, 0x3A, 0x3F); }
inline QColor cardSelected()      { return QColor(0x23, 0x45, 0x3E); }
inline QColor badgeBackground()   { return QColor(0x3E, 0x42, 0x48); }

// Review status of a clip. Distinct in hue *and* paired with a text label
// wherever shown, so status never relies on colour alone.
inline QColor statusNotReviewed() { return textSecondary(); }
inline QColor statusInReview()    { return QColor(0x4C, 0x9E, 0xE0); }
inline QColor statusNeedsChanges(){ return QColor(0xE0, 0x6C, 0x5A); }
inline QColor statusApproved()    { return accent(); }

inline QColor timelineTrack()     { return QColor(0x1B, 0x1D, 0x20); }
inline QColor timelineFill()      { return QColor(0x3A, 0x3E, 0x44); }
inline QColor timelineRange()     { return QColor(0x24, 0x55, 0x4B); }
inline QColor playhead()          { return QColor(0xE8, 0x9A, 0x3C); }
inline QColor tickMark()          { return QColor(0x54, 0x5A, 0x62); }
/// The large current-frame readout beside the timeline: yellow so the eye
/// finds it at a glance.
inline QColor currentFrameNumber() { return QColor(0xFF, 0xD4, 0x3B); }

/// Waveform. A desaturated blue-grey rather than the accent colour: the
/// waveform covers a lot of the timeline, and using the accent would make the
/// playhead -- the thing the eye needs to find -- compete with it.
inline QColor waveform()          { return QColor(0x6E, 0x8A, 0xA6); }
/// Marks silent regions, so "quiet" reads differently from "not analysed yet".
inline QColor waveformSilence()   { return QColor(0x44, 0x4A, 0x52); }

/// Shared sizes, so spacing and rounding stay consistent between widgets that
/// are styled in code and those styled by the stylesheet.
namespace metrics {
inline constexpr int radiusSmall = 4;  // buttons, inputs, chips
inline constexpr int radiusMedium = 6; // cards, popovers
inline constexpr int spacingSmall = 4;
inline constexpr int spacingMedium = 8;
inline constexpr int spacingLarge = 16;
inline constexpr int iconSmall = 16;   // menus, badges
inline constexpr int iconMedium = 18;  // toolbars, transport
inline constexpr int iconLarge = 22;   // primary tool row
inline constexpr int iconTransport = 26; // first / step / skip / last: easy to hit
inline constexpr int iconPlay = 34;      // play / pause: the largest target
inline constexpr int currentFrameFontPx = 30; // current-frame readout, about the track's height
} // namespace metrics

/// Application-wide stylesheet built from the palette above.
QString styleSheet();

} // namespace atk::ui::theme
