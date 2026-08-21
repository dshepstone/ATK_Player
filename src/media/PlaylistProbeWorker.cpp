#include "media/PlaylistProbeWorker.h"

#include "media/MediaDecoder.h"

#include <QFileInfo>

namespace atk::media {

PlaylistProbeWorker::PlaylistProbeWorker(QObject* parent) : QObject(parent) {}

void PlaylistProbeWorker::probe(const QUuid& sourceId, const QString& path, quint64 token)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        emit probeFinished(sourceId, path, token, {}, QStringLiteral("Media file is missing."), true);
        return;
    }
    MediaDecoder decoder;
    QString error;
    if (!decoder.open(path, &error)) {
        emit probeFinished(sourceId, path, token, {}, error, false);
        return;
    }
    emit probeFinished(sourceId, path, token, decoder.metadata(), {}, false);
}

} // namespace atk::media
