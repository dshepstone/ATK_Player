#include "api/ApiServer.h"

#include "core/Logging.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>

#include <utility>

namespace atk::api {

ApiServer::ApiServer(playback::PlaybackController* playback,
                     timeline::TimelineModel* timeline,
                     ApiCommandDispatcher::ApplicationCommandHandler applicationHandler,
                     QObject* parent)
    : QObject(parent)
    , m_dispatcher(std::make_unique<ApiCommandDispatcher>(
          playback, timeline, std::move(applicationHandler), [this] {
              return QJsonObject{
                  {QStringLiteral("protocolVersion"), 1},
                  {QStringLiteral("application"), QStringLiteral("ATK Player")},
                  {QStringLiteral("port"), static_cast<int>(m_port)},
                  {QStringLiteral("frameIndexBase"), 0},
                  {QStringLiteral("maxRequestBytes"),
                   static_cast<double>(kMaximumRequestBytes)},
              };
          }))
    , m_server(std::make_unique<QTcpServer>(this))
{
    connect(m_server.get(), &QTcpServer::newConnection,
            this, &ApiServer::acceptConnections);
}

ApiServer::~ApiServer()
{
    stop();
}

bool ApiServer::start(quint16 port)
{
    stop();
    m_port = port;
    m_errorString.clear();
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        m_errorString = m_server->errorString();
        qCWarning(log::api) << "Unable to listen on loopback port" << port
                            << m_errorString;
        emit errorChanged(m_errorString);
        return false;
    }
    m_port = m_server->serverPort();
    m_running = true;
    qCInfo(log::api) << "Local API listening on 127.0.0.1:" << m_port;
    emit errorChanged({});
    emit runningChanged(true);
    return true;
}

void ApiServer::stop()
{
    const bool wasRunning = m_running;
    m_server->close();
    const auto sockets = m_buffers.keys();
    for (QTcpSocket* socket : sockets) disconnectClient(socket);
    m_running = false;
    if (wasRunning) {
        qCInfo(log::api) << "Local API stopped";
        emit runningChanged(false);
    }
}

ApiResponse ApiServer::handleRequest(const QJsonObject& request)
{
    return m_dispatcher->dispatch(request);
}

void ApiServer::acceptConnections()
{
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        m_buffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { readClient(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
        qCDebug(log::api) << "Local API client connected";
    }
}

void ApiServer::readClient(QTcpSocket* socket)
{
    if (!m_buffers.contains(socket)) return;
    QByteArray& buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > kMaximumRequestBytes && !buffer.contains('\n')) {
        writeResponse(socket,
                      ApiResponse::failure(QStringLiteral("request_too_large")).toJson());
        socket->disconnectFromHost();
        return;
    }

    qsizetype newline = -1;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.size() > kMaximumRequestBytes) {
            writeResponse(socket,
                          ApiResponse::failure(QStringLiteral("request_too_large")).toJson());
            socket->disconnectFromHost();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeResponse(socket, ApiResponse::failure(
                QStringLiteral("invalid_json: request must be a JSON object")).toJson());
            continue;
        }
        const QJsonObject request = document.object();
        writeResponse(socket,
                      handleRequest(request).toJson(request.value(QStringLiteral("id"))));
        if (socket->bytesToWrite() > kMaximumPendingWriteBytes) {
            qCWarning(log::api) << "Disconnecting client with excessive pending output";
            socket->disconnectFromHost();
            return;
        }
    }
}

void ApiServer::writeResponse(QTcpSocket* socket, const QJsonObject& response)
{
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}

void ApiServer::disconnectClient(QTcpSocket* socket)
{
    m_buffers.remove(socket);
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

} // namespace atk::api
