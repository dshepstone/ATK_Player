#include "media/StillImage.h"

#include <QFileInfo>

#include <algorithm>

namespace atk::media {

StillImageOptions StillImageOptions::normalized() const
{
    StillImageOptions result = *this;
    result.holdFrames = std::clamp(holdFrames, kMinimumHoldFrames, kMaximumHoldFrames);
    if (!result.frameRate.isValid()) {
        result.frameRate = kDefaultFrameRate;
    }
    return result;
}

const QStringList& stillImageExtensions()
{
    static const QStringList extensions{
        QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("bmp"),
        QStringLiteral("tga"),  QStringLiteral("webp"), QStringLiteral("exr"),
    };
    return extensions;
}

bool isStillImagePath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return !suffix.isEmpty() && stillImageExtensions().contains(suffix);
}

} // namespace atk::media
