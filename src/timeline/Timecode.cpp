#include "timeline/Timecode.h"

#include <QRegularExpression>

#include <cmath>

namespace atk::timeline::timecode {
namespace {

/// Whole frames per timecode second. 23.976 counts 24 frames per timecode
/// second, 29.97 counts 30 -- the rounded rate, not the exact one.
int nominalFramesPerSecond(media::FrameRate rate)
{
    return static_cast<int>(std::lround(rate.toDouble()));
}

} // namespace

QString placeholder()
{
    return QStringLiteral("--:--:--:--");
}

QString fromFrame(int64_t frame, media::FrameRate rate)
{
    const int fps = rate.isValid() ? nominalFramesPerSecond(rate) : 0;
    if (fps <= 0) {
        return placeholder();
    }

    const bool negative = frame < 0;
    int64_t absFrame = negative ? -frame : frame;

    const int64_t frames  = absFrame % fps;
    const int64_t seconds = (absFrame / fps) % 60;
    const int64_t minutes = (absFrame / (int64_t{ fps } * 60)) % 60;
    const int64_t hours   = absFrame / (int64_t{ fps } * 3600);

    return QStringLiteral("%1%2:%3:%4:%5")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(hours,   2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(frames,  2, 10, QLatin1Char('0'));
}

int64_t toFrame(const QString& text, media::FrameRate rate)
{
    const int fps = rate.isValid() ? nominalFramesPerSecond(rate) : 0;
    if (fps <= 0) {
        return -1;
    }

    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*(-?)(\d{1,3}):([0-5]?\d):([0-5]?\d):(\d{1,3})\s*$)"));

    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return -1;
    }

    const int64_t frames = match.captured(5).toLongLong();
    if (frames >= fps) {
        return -1;
    }

    const int64_t total = match.captured(2).toLongLong() * fps * 3600
                        + match.captured(3).toLongLong() * fps * 60
                        + match.captured(4).toLongLong() * fps
                        + frames;

    return match.captured(1).isEmpty() ? total : -total;
}

} // namespace atk::timeline::timecode
