#include "media/ffmpeg/FFmpegUtil.h"

#include "media/MediaLibraryInfo.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <array>

namespace atk::media::ffmpeg {
namespace {

/// Microseconds as an AVRational time base, for av_rescale_q.
constexpr AVRational kMicrosecondTimeBase{ 1, 1'000'000 };

QString versionTriplet(unsigned int packed)
{
    return QStringLiteral("%1.%2.%3")
        .arg(AV_VERSION_MAJOR(packed))
        .arg(AV_VERSION_MINOR(packed))
        .arg(AV_VERSION_MICRO(packed));
}

} // namespace

QString errorString(int errorCode)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    if (av_strerror(errorCode, buffer.data(), buffer.size()) < 0) {
        // av_strerror itself failed, which means the code is not one it knows.
        return QStringLiteral("unknown FFmpeg error (%1)").arg(errorCode);
    }
    return QString::fromUtf8(buffer.data());
}

QString errorString(const char* context, int errorCode)
{
    return QStringLiteral("%1: %2 (%3)")
        .arg(QString::fromUtf8(context), errorString(errorCode))
        .arg(errorCode);
}

QString libraryVersions()
{
    return QStringLiteral("avformat %1, avcodec %2, avutil %3, swscale %4, swresample %5")
        .arg(versionTriplet(avformat_version()),
             versionTriplet(avcodec_version()),
             versionTriplet(avutil_version()),
             versionTriplet(swscale_version()),
             versionTriplet(swresample_version()));
}

bool isValidRational(AVRational rational)
{
    return rational.num > 0 && rational.den > 0;
}

int64_t ptsToMicroseconds(int64_t pts, AVRational timeBase)
{
    if (pts == AV_NOPTS_VALUE || !isValidRational(timeBase)) {
        return 0;
    }
    return av_rescale_q(pts, timeBase, kMicrosecondTimeBase);
}

int64_t microsecondsToPts(int64_t microseconds, AVRational timeBase)
{
    if (!isValidRational(timeBase)) {
        return 0;
    }
    return av_rescale_q(microseconds, kMicrosecondTimeBase, timeBase);
}

int64_t frameIndexToPts(int64_t frameIndex, AVRational frameRate,
                        AVRational timeBase, int64_t startTime)
{
    if (!isValidRational(frameRate) || !isValidRational(timeBase)) {
        return startTime == AV_NOPTS_VALUE ? 0 : startTime;
    }

    // frameIndex * (1 / frameRate), expressed in the stream's time base.
    const AVRational frameDuration = av_inv_q(frameRate);
    const int64_t offset = av_rescale_q(frameIndex, frameDuration, timeBase);

    return (startTime == AV_NOPTS_VALUE ? 0 : startTime) + offset;
}

int64_t ptsToFrameIndex(int64_t pts, AVRational frameRate,
                        AVRational timeBase, int64_t startTime)
{
    if (pts == AV_NOPTS_VALUE || !isValidRational(frameRate) || !isValidRational(timeBase)) {
        return 0;
    }

    const int64_t relative = pts - (startTime == AV_NOPTS_VALUE ? 0 : startTime);
    if (relative <= 0) {
        return 0;
    }

    // Round to nearest rather than truncating: a timestamp that sits a tick
    // below the ideal grid position still belongs to its own frame, and
    // truncation would report the previous one.
    const AVRational frameDuration = av_inv_q(frameRate);
    return av_rescale_q_rnd(relative, timeBase, frameDuration,
                            static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}

int64_t frameIndexToMicroseconds(int64_t frameIndex, AVRational frameRate)
{
    if (!isValidRational(frameRate)) {
        return 0;
    }
    return av_rescale_q(frameIndex, av_inv_q(frameRate), kMicrosecondTimeBase);
}

int64_t microsecondsToFrameIndex(int64_t microseconds, AVRational frameRate)
{
    if (!isValidRational(frameRate) || microseconds <= 0) {
        return 0;
    }
    return av_rescale_q(microseconds, kMicrosecondTimeBase, av_inv_q(frameRate));
}

} // namespace atk::media::ffmpeg

namespace atk::media {

QString libraryVersionSummary()
{
    return ffmpeg::libraryVersions();
}

} // namespace atk::media
