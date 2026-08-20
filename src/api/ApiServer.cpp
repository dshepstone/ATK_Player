#include "api/ApiServer.h"

#include "core/Logging.h"

namespace atk::api {

ApiServer::ApiServer(playback::PlaybackController* playback,
                     timeline::TimelineModel* timeline,
                     QObject* parent)
    : QObject(parent)
    , m_dispatcher(std::make_unique<ApiCommandDispatcher>(playback, timeline))
{
}

ApiServer::~ApiServer() = default;

bool ApiServer::start(quint16 port)
{
    m_port = port;
    // TODO(M5): QTcpServer::listen(QHostAddress::LocalHost, port). Until then
    // report failure honestly rather than pretending to be reachable.
    qCInfo(log::api) << "API server requested on loopback port" << port
                     << "-- transport not implemented yet (planned for milestone M5)";
    return false;
}

void ApiServer::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    emit runningChanged(false);
}

ApiResponse ApiServer::handleRequest(const QJsonObject& request)
{
    return m_dispatcher->dispatch(request);
}

} // namespace atk::api
