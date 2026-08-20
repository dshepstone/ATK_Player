#include "export/FFmpegExporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace atk::exporter {

QString FFmpegExporter::validate(const ExportSettings& settings)
{
    if (settings.outputPath.isEmpty()) {
        return QCoreApplication::translate("FFmpegExporter", "No output path was given.");
    }

    const QFileInfo info(settings.outputPath);
    if (!info.absoluteDir().exists()) {
        return QCoreApplication::translate("FFmpegExporter", "The output folder does not exist.");
    }

    if (settings.range.enabled && !settings.range.isValid()) {
        return QCoreApplication::translate("FFmpegExporter", "The export range is invalid.");
    }

    if (settings.outputWidth < 0 || settings.outputHeight < 0) {
        return QCoreApplication::translate("FFmpegExporter", "Output dimensions cannot be negative.");
    }

    return {};
}

bool FFmpegExporter::isSupported()
{
    // TODO(M5): true once the FFmpeg encode path exists.
    return false;
}

} // namespace atk::exporter
