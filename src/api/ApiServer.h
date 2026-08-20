#pragma once

#include "api/ApiCommands.h"

#include <QObject>
#include <QString>

#include <memory>

namespace atk::playback { class PlaybackController; }
namespace atk::timeline { class TimelineModel; }

namespace atk::api {

/// Local control API for external applications.
///
/// PHASE 0 STATUS: no socket is opened. The dispatcher underneath is real and
/// unit-tested; only the transport is missing. start() logs and returns false
/// so nothing believes the API is live.
///
/// SECURITY RULES the implementation must honour (milestone M5):
///   1. Bind to the loopback interface ONLY -- 127.0.0.1 and ::1. Never
///      QHostAddress::Any. A review player must not become a network service
///      on a studio LAN by accident.
///   2. The API is off by default and enabled explicitly in preferences.
///   3. Reject requests larger than a fixed cap so a malformed client cannot
///      exhaust memory.
///   4. File paths arriving over the API are opened as media only; nothing in
///      a request may select code to run.
///   5. Commands execute on the UI thread via a queued connection, because they
///      manipulate models the UI observes.
///
/// PROTOCOL: newline-delimited JSON over TCP. One JSON object per line in each
/// direction, so a client can be written in any language with nothing beyond a
/// socket and a JSON parser.
class ApiServer : public QObject {
    Q_OBJECT

public:
    /// Default port. Registered nowhere; picked from the dynamic range and
    /// overridable in preferences.
    static constexpr quint16 kDefaultPort = 45571;

    ApiServer(playback::PlaybackController* playback,
              timeline::TimelineModel* timeline,
              QObject* parent = nullptr);
    ~ApiServer() override;

    /// Attempts to listen on loopback.
    /// TODO(M5): implement with QTcpServer bound to QHostAddress::LocalHost.
    /// Returns false in Phase 0.
    bool start(quint16 port = kDefaultPort);
    void stop();
    bool isRunning() const { return m_running; }
    quint16 port() const { return m_port; }

    /// Handles one request object. Exposed directly so the dispatcher can be
    /// driven by tests and, later, by an in-process integration without a
    /// socket round trip.
    ApiResponse handleRequest(const QJsonObject& request);

signals:
    void runningChanged(bool running);

private:
    std::unique_ptr<ApiCommandDispatcher> m_dispatcher;
    quint16 m_port = kDefaultPort;
    bool m_running = false;
};

} // namespace atk::api
