#pragma once

#include "annotation/BrushTip.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace atk::annotation {

/// Result of reading a Photoshop brush set.
struct AbrReadResult {
    QVector<BrushTip> tips;
    /// Presets in the file that have no image tip (Photoshop "computed" round
    /// brushes); they are skipped and counted so the UI can say so.
    int skippedComputed = 0;
    QString error; // empty on success
    bool ok() const { return error.isEmpty(); }
};

/// Reads the brush tips out of a Photoshop .abr file.
///
/// Supports the two layouts Photoshop has written:
///   - versions 1 and 2 (Photoshop up to 6): a count of brushes, sampled ones
///     carrying their image inline;
///   - versions 6, 7 and 10 (Photoshop 7 to current): tagged "8BIM" sections,
///     where "samp" holds the tip images and "desc" the presets (names,
///     spacing, angle) that refer to them by UUID.
/// Images may be raw or PackBits-compressed, 8 or 16 bits deep.
///
/// Only the tip image, name, spacing, angle and diameter are imported; brush
/// dynamics, textures, dual brushes and the like have no equivalent here.
/// Every length and size is bounds-checked: a damaged or hostile file yields
/// an error or a partial result, never a crash or a huge allocation.
AbrReadResult readAbr(const QByteArray& data, const QString& fallbackNamePrefix = QStringLiteral("Brush"));

} // namespace atk::annotation
