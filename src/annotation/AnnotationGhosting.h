#pragma once

#include "annotation/AnnotationTypes.h"

#include <QColor>
#include <QVector>

namespace atk::annotation {

/// Onion-skin display of drawings from nearby frames ("ghosts"). A view aid
/// only: it never changes annotation data and is not burned into exports.
struct GhostSettings {
    static constexpr int kMaxRange = 30;

    /// How `before` and `after` are counted.
    enum class Mode {
        Frames,   ///< every drawn frame within N timeline frames
        Drawings, ///< the N nearest drawn frames, however far away
    };

    bool enabled = false;
    int before = 3;
    int after = 3;
    Mode mode = Mode::Frames;
    bool tint = true; ///< red for earlier frames, green for later ones

    friend bool operator==(const GhostSettings&, const GhostSettings&) = default;
};

/// One ghosted frame, ready to paint.
struct GhostFrame {
    FrameAnnotation frame;
    int offset = 0;       ///< frame distance from the current frame (negative = earlier)
    int rank = 1;         ///< 1 = nearest ghost on its side
    qreal opacity = 0.5;  ///< fades with rank
    QColor tint;          ///< invalid = keep the drawing's own colours

    friend bool operator==(const GhostFrame&, const GhostFrame&) = default;
};

/// Colours used when tinting is on.
QColor ghostBeforeColor();
QColor ghostAfterColor();

/// Drawings to ghost around `current`, farthest first so nearer ghosts paint
/// on top. Frames with only notes are skipped; the current frame never ghosts.
QVector<GhostFrame> ghostFramesFor(const SourceAnnotations& annotations, int64_t current,
                                   const GhostSettings& settings);

/// Opacity for the `rank`-th ghost (1 = nearest) out of `count` on one side.
qreal ghostOpacity(int rank, int count);

} // namespace atk::annotation
