#include "media/MediaMetadata.h"

#include <QStringList>

namespace atk::media {

int64_t MediaMetadata::effectiveFrameCount() const
{
    if (frameCount >= 0) {
        return frameCount;
    }
    if (durationUs >= 0 && frameRate.isValid()) {
        // durationUs * (num / den) / 1'000'000, in integer arithmetic to avoid
        // rounding a long clip off by a frame.
        const auto num = static_cast<int64_t>(frameRate.numerator);
        const auto den = static_cast<int64_t>(frameRate.denominator);
        return (durationUs * num) / (den * 1'000'000);
    }
    return -1;
}

double MediaMetadata::durationSeconds() const
{
    return durationUs >= 0 ? static_cast<double>(durationUs) / 1'000'000.0 : -1.0;
}

QString MediaMetadata::shortDescription() const
{
    QStringList parts;

    if (hasVideo && !resolution.isEmpty()) {
        parts << QStringLiteral("%1x%2").arg(resolution.width()).arg(resolution.height());
    }
    if (isImageSequence) {
        parts << QStringLiteral("Image sequence");
        if (frameCount > 0 && frameRate.isValid()) {
            parts << QStringLiteral("%1 frames @ %2 fps")
                         .arg(frameCount)
                         .arg(QString::number(frameRate.toDouble(), 'g', 5));
        }
        if (sequenceMissingFrames > 0) {
            parts << QStringLiteral("%1 missing").arg(sequenceMissingFrames);
        }
        return parts.join(QStringLiteral(" • "));
    }
    if (isStillImage) {
        // The rate and duration describe the hold, not the file, so say so
        // rather than presenting a picture as if it were a two-second clip.
        parts << QStringLiteral("Still image");
        if (frameCount > 0 && frameRate.isValid()) {
            parts << QStringLiteral("%1 frames @ %2 fps")
                         .arg(frameCount)
                         .arg(QString::number(frameRate.toDouble(), 'g', 5));
        }
        return parts.join(QStringLiteral(" • "));
    }
    if (frameRate.isValid()) {
        // 'g' with 5 significant digits keeps 23.976 readable without turning
        // 24 into "24.000".
        parts << QStringLiteral("%1 fps").arg(QString::number(frameRate.toDouble(), 'g', 5));
    }
    if (durationUs >= 0) {
        parts << QStringLiteral("%1s").arg(QString::number(durationSeconds(), 'f', 2));
    }
    if (hasAudio) {
        parts << QStringLiteral("%1 Hz %2ch").arg(audioSampleRate).arg(audioChannelCount);
    }

    return parts.join(QStringLiteral(" • "));
}

} // namespace atk::media
