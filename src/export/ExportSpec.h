#pragma once

#include "media/MediaMetadata.h"
#include "playback/CompareSession.h"
#include <QString>
#include <QUuid>
#include <QVector>

namespace atk::exporter {

enum class ExportKind { ReviewVideo, CurrentFrame, ImageSequence };

struct ExportBurnIns {
    bool frameNumber = false;
    bool bookmarkLabels = false;
    bool bookmarkNotes = false;

    bool enabled() const { return frameNumber || bookmarkLabels || bookmarkNotes; }
};

struct ExportBookmark {
    quint64 id = 0;
    bool range = false;
    qint64 startFrame = 0;
    qint64 endFrame = 0;
    QString name;
    QString note;
    int colorIndex = -1;
};

struct ExportSource {
    QUuid id;
    QString path;
    media::MediaMetadata metadata;
    qint64 rangeStartFrame = 0;
    qint64 rangeEndFrame = 0;
};

/// Immutable value snapshot consumed by an offline export job.
struct ExportSpec {
    ExportKind kind = ExportKind::ReviewVideo;
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
    QString imagePrefix;
    ExportBurnIns burnIns;
    QVector<ExportBookmark> bookmarks;
    /// Optional output subset. Mapping origins remain the source review ranges.
    qint64 firstFrame = -1;
    qint64 lastFrame = -1;
    QString videoEncoder = QStringLiteral("h264_mf");
    QString audioEncoder = QStringLiteral("aac");
    int audioSampleRate = 48'000;
    int audioChannels = 2;

    QSize contentSize() const;
    QSize renderSize() const;
    QSize outputSize() const;
    qint64 frameCount() const;
    qint64 exportStartFrame() const;
    qint64 exportEndFrame() const;
    QString audioSummary() const;
    QString validate() const;
};

} // namespace atk::exporter
