#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace atk::playback { class PlaybackController; }
namespace atk::timeline { class TimelineModel; }

namespace atk::api {

/// Reply to one API request.
///
/// Serialised as:
///     { "id": 7, "ok": true,  "result": { ... } }
///     { "id": 7, "ok": false, "error": "unknown command: frobnicate" }
struct ApiResponse {
    bool ok = false;
    QJsonObject result;
    QString error;

    static ApiResponse success(QJsonObject result = {});
    static ApiResponse failure(QString error);

    QJsonObject toJson(const QJsonValue& requestId = {}) const;
};

/// Translates JSON command objects into calls on the player.
///
/// This is the whole external API surface. It is deliberately separate from the
/// transport that carries it: the dispatcher is a pure request-in/response-out
/// object, so it is unit-testable with no socket, and a future transport (TCP,
/// named pipe, WebSocket) only has to move bytes.
///
/// Requests look like:
///     { "id": 1, "command": "seek_frame", "params": { "frame": 42 } }
///
/// See docs/API.md for the full command reference.
class ApiCommandDispatcher {
public:
    /// Neither pointer is owned; both must outlive the dispatcher.
    ApiCommandDispatcher(playback::PlaybackController* playback,
                         timeline::TimelineModel* timeline);

    ApiResponse dispatch(const QJsonObject& request);

    /// Command names this build understands, for the "list_commands" reply and
    /// for the Python client to validate against.
    static QStringList supportedCommands();

private:
    playback::PlaybackController* m_playback = nullptr;
    timeline::TimelineModel* m_timeline = nullptr;
};

} // namespace atk::api
