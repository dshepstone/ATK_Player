#include "export/ExportSpec.h"
#include "playback/ComparisonCompositor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace atk::exporter {

QSize ExportSpec::contentSize() const
{
    const QSize a = sourceA.metadata.resolution;
    return comparison ? playback::ComparisonCompositor::canvasSize(a, layout) : a;
}

QSize ExportSpec::outputSize() const
{
    QSize value = contentSize();
    if (value.width() % 2) value.setWidth(value.width() + 1);
    if (value.height() % 2) value.setHeight(value.height() + 1);
    return value;
}

QSize ExportSpec::renderSize() const
{
    return kind == ExportKind::ReviewVideo ? outputSize() : contentSize();
}

qint64 ExportSpec::frameCount() const
{
    return exportEndFrame() >= exportStartFrame()
        ? exportEndFrame() - exportStartFrame() + 1 : 0;
}

qint64 ExportSpec::exportStartFrame() const
{
    return firstFrame >= 0 ? firstFrame : sourceA.rangeStartFrame;
}

qint64 ExportSpec::exportEndFrame() const
{
    return lastFrame >= 0 ? lastFrame : sourceA.rangeEndFrame;
}

QString ExportSpec::audioSummary() const
{
    if (!comparison || audioMode == playback::CompareAudioMode::SourceA)
        return sourceA.metadata.hasAudio ? QStringLiteral("Source A") : QStringLiteral("None");
    if (audioMode == playback::CompareAudioMode::SourceB)
        return sourceB.metadata.hasAudio ? QStringLiteral("Source B") : QStringLiteral("None");
    return externalAudioPath.isEmpty() ? QStringLiteral("None") : QStringLiteral("External");
}

QString ExportSpec::validate() const
{
    if (!QFileInfo::exists(sourceA.path)) return QCoreApplication::translate("ExportSpec", "Source A is unavailable.");
    if (comparison && !QFileInfo::exists(sourceB.path)) return QCoreApplication::translate("ExportSpec", "Source B is unavailable.");
    if (comparison && audioMode == playback::CompareAudioMode::External
        && !externalAudioPath.isEmpty() && !QFileInfo::exists(externalAudioPath))
        return QCoreApplication::translate("ExportSpec", "External Audio is unavailable.");
    if (outputPath.isEmpty()) return QCoreApplication::translate("ExportSpec", "Choose an output destination.");
    const QFileInfo output(outputPath);
    if (!output.absoluteDir().exists()) return QCoreApplication::translate("ExportSpec", "The output folder does not exist.");
    if (kind == ExportKind::ImageSequence && output.exists())
        return QCoreApplication::translate("ExportSpec", "The image-sequence folder already exists.");
    if (kind == ExportKind::ImageSequence && imagePrefix.trimmed().isEmpty())
        return QCoreApplication::translate("ExportSpec", "Choose an image-sequence filename prefix.");
    if (QFileInfo(sourceA.path).absoluteFilePath() == output.absoluteFilePath()
        || (comparison && QFileInfo(sourceB.path).absoluteFilePath() == output.absoluteFilePath()))
        return QCoreApplication::translate("ExportSpec", "The export cannot overwrite source media.");
    if (frameCount() <= 0 || renderSize().isEmpty()) return QCoreApplication::translate("ExportSpec", "The active review range is invalid.");
    const qint64 sourceLast = sourceA.metadata.effectiveFrameCount() - 1;
    if (exportStartFrame() < 0 || exportEndFrame() > sourceLast)
        return QCoreApplication::translate("ExportSpec", "The requested frames are outside Source A.");
    if (kind == ExportKind::CurrentFrame && frameCount() != 1)
        return QCoreApplication::translate("ExportSpec", "Current-frame export requires exactly one frame.");
    if (kind == ExportKind::ReviewVideo && videoEncoder.isEmpty())
        return QCoreApplication::translate("ExportSpec", "No compatible H.264 encoder is available.");
    return {};
}

} // namespace atk::exporter
