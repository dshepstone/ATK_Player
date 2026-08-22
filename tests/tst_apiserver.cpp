#include "api/ApiServer.h"
#include "playback/PlaybackController.h"
#include "timeline/TimelineModel.h"

#include <QHostAddress>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTest>

using atk::api::ApiServer;

class TestApiServer : public QObject {
    Q_OBJECT
private slots:
    void loopbackLifecycleAndRequestIds();
    void streamFramingAndMultipleClients();
    void malformedAndOversizedRequestsRecover();
    void navigationResponsesExposeAcceptedTargets();
};

namespace {
struct Fixture {
    atk::timeline::TimelineModel timeline;
    atk::playback::PlaybackController playback{&timeline};
    ApiServer server{&playback, &timeline};
    Fixture() { timeline.setFrameCount(264); }
};

QJsonObject response(QTcpSocket& socket)
{
    QElapsedTimer timer;
    timer.start();
    while (!socket.canReadLine() && timer.elapsed() < 3000) {
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }
    if (!socket.canReadLine()) return {};
    return QJsonDocument::fromJson(socket.readLine().trimmed()).object();
}

void connectClient(QTcpSocket& socket, quint16 port)
{
    socket.connectToHost(QHostAddress::LocalHost, port);
    QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QAbstractSocket::ConnectedState, 3000);
}
}

void TestApiServer::loopbackLifecycleAndRequestIds()
{
    Fixture fixture;
    QVERIFY(fixture.server.start(0));
    QVERIFY(fixture.server.isRunning());
    QVERIFY(fixture.server.port() != 0);

    QTcpSocket client;
    connectClient(client, fixture.server.port());
    client.write("{\"id\":\"maya-7\",\"command\":\"get_api_info\"}\n");
    const QJsonObject reply = response(client);
    QCOMPARE(reply.value(QStringLiteral("id")).toString(), QStringLiteral("maya-7"));
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("frameIndexBase")).toInt(), 0);
    QCOMPARE(reply.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("port")).toInt(), fixture.server.port());

    fixture.server.stop();
    QVERIFY(!fixture.server.isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 3000);
}

void TestApiServer::streamFramingAndMultipleClients()
{
    Fixture fixture;
    QVERIFY(fixture.server.start(0));
    QTcpSocket first;
    QTcpSocket second;
    connectClient(first, fixture.server.port());
    connectClient(second, fixture.server.port());

    first.write("{\"id\":1,\"command\":\"get_");
    QTest::qWait(10);
    first.write("current_frame\"}\n{\"id\":2,\"command\":\"list_commands\"}\n");
    QCOMPARE(response(first).value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(response(first).value(QStringLiteral("id")).toInt(), 2);

    second.write("{\"id\":3,\"command\":\"get_status\"}\n");
    const QJsonObject reply = response(second);
    QCOMPARE(reply.value(QStringLiteral("id")).toInt(), 3);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
}

void TestApiServer::malformedAndOversizedRequestsRecover()
{
    Fixture fixture;
    QVERIFY(fixture.server.start(0));
    QTcpSocket client;
    connectClient(client, fixture.server.port());
    client.write("[]\nnot-json\n");
    QVERIFY(!response(client).value(QStringLiteral("ok")).toBool());
    QVERIFY(!response(client).value(QStringLiteral("ok")).toBool());

    client.write(QByteArray(ApiServer::kMaximumRequestBytes + 1, 'x'));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QAbstractSocket::UnconnectedState, 3000);

    QTcpSocket recovered;
    connectClient(recovered, fixture.server.port());
    recovered.write("{\"id\":4,\"command\":\"get_api_info\"}\n");
    QVERIFY(response(recovered).value(QStringLiteral("ok")).toBool());
}

void TestApiServer::navigationResponsesExposeAcceptedTargets()
{
    Fixture fixture;
    QVERIFY(fixture.server.start(0));
    QTcpSocket client;
    connectClient(client, fixture.server.port());

    client.write("{\"id\":1,\"command\":\"seek_frame\",\"params\":{\"frame\":100}}\n");
    QJsonObject result = response(client).value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(result.value(QStringLiteral("targetFrame")).toInt(), 100);
    QVERIFY(!result.contains(QStringLiteral("currentFrame")));

    for (int id = 2; id <= 6; ++id)
        client.write(QStringLiteral("{\"id\":%1,\"command\":\"step_forward\"}\n")
                         .arg(id).toUtf8());
    for (int expected = 101; expected <= 105; ++expected) {
        result = response(client).value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("targetFrame")).toInt(), expected);
    }
    QCOMPARE(fixture.timeline.currentFrame(), qint64(105));

    for (int id = 7; id <= 11; ++id)
        client.write(QStringLiteral("{\"id\":%1,\"command\":\"step_backward\"}\n")
                         .arg(id).toUtf8());
    for (int expected = 104; expected >= 100; --expected)
        QCOMPARE(response(client).value(QStringLiteral("result")).toObject()
                     .value(QStringLiteral("targetFrame")).toInt(), expected);
    QCOMPARE(fixture.timeline.currentFrame(), qint64(100));
}

QTEST_MAIN(TestApiServer)
#include "tst_apiserver.moc"
