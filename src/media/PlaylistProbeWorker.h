#pragma once

#include "media/MediaMetadata.h"

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
    void probe(const QUuid& sourceId, const QString& path, quint64 token);

signals:
    void probeFinished(QUuid sourceId, QString path, quint64 token,
                       atk::media::MediaMetadata metadata, QString error,
                       bool missing);
};

} // namespace atk::media
