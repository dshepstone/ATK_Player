#include "timeline/Bookmark.h"

#include <array>

namespace atk::timeline {
namespace {

// Chosen to stay distinguishable against the dark timeline background and to
// remain separable for the most common forms of colour blindness.
constexpr std::array<QRgb, 8> kPalette{
    qRgb(0xE5, 0x4B, 0x4B), // red
    qRgb(0xE5, 0x8F, 0x33), // orange
    qRgb(0xE5, 0xC8, 0x3D), // yellow
    qRgb(0x64, 0xC2, 0x5A), // green
    qRgb(0x3D, 0xB8, 0xC8), // cyan
    qRgb(0x4C, 0x8C, 0xE5), // blue
    qRgb(0x9B, 0x6B, 0xE5), // violet
    qRgb(0xE0, 0xE0, 0xE0), // neutral
};

} // namespace

QString Bookmark::displayLabel() const
{
    return hasName() ? name : frameLabel();
}

QString Bookmark::frameLabel() const
{
    return isRange()
        ? QStringLiteral("%1–%2").arg(frame + 1).arg(endFrame + 1)
        : QString::number(frame + 1);
}

QColor bookmarkColor(int colorIndex)
{
    if (colorIndex < 0 || colorIndex >= static_cast<int>(kPalette.size())) {
        return {};
    }
    return QColor::fromRgb(kPalette[static_cast<std::size_t>(colorIndex)]);
}

int bookmarkColorCount()
{
    return static_cast<int>(kPalette.size());
}

} // namespace atk::timeline
