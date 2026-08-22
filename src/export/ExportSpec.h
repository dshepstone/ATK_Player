#pragma once

#include "media/MediaMetadata.h"
#include "playback/CompareSession.h"
#include <QString>
#include <QUuid>

namespace atk::exporter {

struct ExportSource {
    QUuid id;
    QString path;
    media::MediaMetadata metadata;
    qint64 rangeStartFrame = 0;
    qint64 rangeEndFrame = 0;
};

/// Immutable value snapshot consumed by an offline export job.
struct ExportSpec {
    ExportSource sourceA;
    ExportSource sourceB;
    bool comparison = false;
    playback::CompareLayout layout = playback::CompareLayout::SideBySide;
    qint64 sourceBOffsetUs = 0;
    int wipePosition = 50;
    int blendAmount = 50;
    playback::CompareAudioMode audioMode = playback::CompareAudioMode::SourceA;
    QString externalAudioPath;
    qint64 externalAudioOffsetUs = 0;
    QString outputPath;
    QString videoEncoder = QStringLiteral("h264_mf");
    QString audioEncoder = QStringLiteral("aac");
    int audioSampleRate = 48'000;
    int audioChannels = 2;

    QSize contentSize() const;
    QSize outputSize() const;
    qint64 frameCount() const;
    QString audioSummary() const;
    QString validate() const;
};

} // namespace atk::exporter
