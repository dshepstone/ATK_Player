#include "annotation/AnnotationGhosting.h"

#include <algorithm>

namespace atk::annotation {

QColor ghostBeforeColor() { return QColor(235, 70, 70); }
QColor ghostAfterColor() { return QColor(60, 200, 110); }

qreal ghostOpacity(int rank, int count)
{
    count = std::max(1, count);
    rank = std::clamp(rank, 1, count);
    // Nearest 0.55, fading to 0.15 at the far end.
    return count == 1 ? 0.55 : 0.55 - 0.40 * qreal(rank - 1) / qreal(count - 1);
}

QVector<GhostFrame> ghostFramesFor(const SourceAnnotations& annotations, int64_t current,
                                   const GhostSettings& settings)
{
    QVector<GhostFrame> ghosts;
    if (!settings.enabled || current < 0) return ghosts;
    const int before = std::clamp(settings.before, 0, GhostSettings::kMaxRange);
    const int after = std::clamp(settings.after, 0, GhostSettings::kMaxRange);
    const auto& frames = annotations.frames();

    // Collect each side nearest first.
    QVector<const FrameAnnotation*> earlier;
    QVector<const FrameAnnotation*> later;
    if (before > 0) {
        auto it = frames.lowerBound(current);
        while (it != frames.cbegin()) {
            --it;
            if (settings.mode == GhostSettings::Mode::Frames && current - it.key() > before) break;
            if (!it->hasDrawings()) continue;
            earlier.append(&it.value());
            if (settings.mode == GhostSettings::Mode::Drawings && earlier.size() >= before) break;
        }
    }
    if (after > 0) {
        for (auto it = frames.upperBound(current); it != frames.cend(); ++it) {
            if (settings.mode == GhostSettings::Mode::Frames && it.key() - current > after) break;
            if (!it->hasDrawings()) continue;
            later.append(&it.value());
            if (settings.mode == GhostSettings::Mode::Drawings && later.size() >= after) break;
        }
    }

    // Opacity fades over the configured reach, so a ghost three frames away
    // looks the same whether or not frames in between are drawn.
    auto add = [&](const QVector<const FrameAnnotation*>& side, int reach, const QColor& tint) {
        for (int i = int(side.size()) - 1; i >= 0; --i) {
            GhostFrame ghost;
            ghost.frame = *side[i];
            ghost.offset = int(side[i]->frame - current);
            ghost.rank = i + 1;
            const int distance = settings.mode == GhostSettings::Mode::Frames ? std::abs(ghost.offset) : ghost.rank;
            ghost.opacity = ghostOpacity(distance, reach);
            if (settings.tint) ghost.tint = tint;
            ghosts.append(std::move(ghost));
        }
    };
    add(earlier, before, ghostBeforeColor());
    add(later, after, ghostAfterColor());
    // Farthest first overall, so the nearest ghosts end up on top.
    std::stable_sort(ghosts.begin(), ghosts.end(),
                     [](const GhostFrame& a, const GhostFrame& b) { return std::abs(a.offset) > std::abs(b.offset); });
    return ghosts;
}

} // namespace atk::annotation
