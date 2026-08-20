#include "export/ExportJob.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace atk::exporter {

QString ExportJob::validate(const ExportSettings& settings)
{
    if (settings.outputPath.isEmpty()) {
        return QCoreApplication::translate("ExportJob", "No output path was given.");
    }

    const QFileInfo info(settings.outputPath);
    if (!info.absoluteDir().exists()) {
        return QCoreApplication::translate("ExportJob", "The output folder does not exist.");
    }

    if (settings.range.enabled && !settings.range.isValid()) {
        return QCoreApplication::translate("ExportJob", "The export range is invalid.");
    }

    if (settings.outputWidth < 0 || settings.outputHeight < 0) {
        return QCoreApplication::translate("ExportJob", "Output dimensions cannot be negative.");
    }

    return {};
}

bool ExportJob::isSupported()
{
    // TODO(M5): true once the FFmpeg encode path exists.
    return false;
}

} // namespace atk::exporter
