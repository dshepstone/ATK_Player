#pragma once

#include "media/MediaMetadata.h"
#include "media/StillImage.h"

#include <QObject>
#include <QUuid>

namespace atk::media {

/// Owns one short-lived MediaDecoder per request on a dedicated probe thread.
/// It reads container/stream metadata only; it never decodes frames or audio.
class PlaylistProbeWorker : public QObject {
    Q_OBJECT
public:
    explicit PlaylistProbeWorker(QObject* parent = nullptr);

public slots:
    /// `still` is the source's hold when `path` is a still image: the probed
    /// frame count of a still is the hold, so it must match playback's.
    void probe(const QUuid& sourceId, const QString& path, quint64 token,
               const atk::media::StillImageOptions& still = {});

signals:
    void probeFinished(QUuid sourceId, QString path, quint64 token,
                       atk::media::MediaMetadata metadata, QString error,
                       bool missing);
};

} // namespace atk::media
