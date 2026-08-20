#include "api/ApiCommands.h"

#include "playback/PlaybackController.h"
#include "timeline/TimelineModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using atk::api::ApiCommandDispatcher;
using atk::api::ApiResponse;
using atk::media::FrameRate;
using atk::playback::PlaybackController;
using atk::timeline::TimelineModel;

namespace {

QJsonObject request(const QString& command, QJsonObject params = {})
{
    QJsonObject object;
    object.insert(QStringLiteral("command"), command);
    if (!params.isEmpty()) {
        object.insert(QStringLiteral("params"), params);
    }
    return object;
}

} // namespace

class TestApiCommands : public QObject {
    Q_OBJECT

private:
    /// A ready-to-drive player with 100 frames at 24 fps.
    struct Fixture {
        TimelineModel timeline;
        PlaybackController playback{ &timeline };
        ApiCommandDispatcher dispatcher{ &playback, &timeline };

        Fixture()
        {
            timeline.setFrameRate(FrameRate::fromInteger(24));
            timeline.setFrameCount(100);
        }
    };

private slots:
    void rejectsRequestWithoutCommand();
    void rejectsUnknownCommand();
    void listsItsOwnCommands();
    void reportsStatus();
    void seeksToFrame();
    void rejectsSeekWithoutFrame();
    void rejectsSeekWithWrongType();
    void clampsSeekToExtent();
    void stepsForwardAndBackward();
    void togglesLoop();
    void setsLoopRange();
    void addsBookmarkAtCurrentFrameByDefault();
    void addsBookmarkAtGivenFrame();
    void reportsUnimplementedCommandsExplicitly();
    void responseSerialisesWithId();
};

void TestApiCommands::rejectsRequestWithoutCommand()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(QJsonObject{});
    QVERIFY(!response.ok);
    QVERIFY(response.error.contains(QStringLiteral("command")));
}

void TestApiCommands::rejectsUnknownCommand()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(request(QStringLiteral("frobnicate")));
    QVERIFY(!response.ok);
    QVERIFY(response.error.contains(QStringLiteral("unknown command")));
}

void TestApiCommands::listsItsOwnCommands()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(request(QStringLiteral("list_commands")));
    QVERIFY(response.ok);

    const QJsonArray commands = response.result.value(QStringLiteral("commands")).toArray();
    QCOMPARE(commands.size(), ApiCommandDispatcher::supportedCommands().size());
    QVERIFY(commands.contains(QJsonValue(QStringLiteral("seek_frame"))));
}

void TestApiCommands::reportsStatus()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(request(QStringLiteral("get_status")));
    QVERIFY(response.ok);
    QCOMPARE(response.result.value(QStringLiteral("state")).toString(), QStringLiteral("stopped"));
    QCOMPARE(response.result.value(QStringLiteral("frameCount")).toDouble(), 100.0);
    QCOMPARE(response.result.value(QStringLiteral("fps")).toDouble(), 24.0);
    QCOMPARE(response.result.value(QStringLiteral("hasMedia")).toBool(), false);
}

void TestApiCommands::seeksToFrame()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(
        request(QStringLiteral("seek_frame"), { { QStringLiteral("frame"), 42 } }));

    QVERIFY(response.ok);
    QCOMPARE(fixture.timeline.currentFrame(), qint64(42));
    QCOMPARE(response.result.value(QStringLiteral("currentFrame")).toDouble(), 42.0);
}

void TestApiCommands::rejectsSeekWithoutFrame()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(request(QStringLiteral("seek_frame")));
    QVERIFY(!response.ok);
    QVERIFY(response.error.contains(QStringLiteral("frame")));
}

void TestApiCommands::rejectsSeekWithWrongType()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(
        request(QStringLiteral("seek_frame"), { { QStringLiteral("frame"), QStringLiteral("ten") } }));

    QVERIFY(!response.ok);
    QCOMPARE(fixture.timeline.currentFrame(), qint64(0));
}

void TestApiCommands::clampsSeekToExtent()
{
    Fixture fixture;
    fixture.dispatcher.dispatch(
        request(QStringLiteral("seek_frame"), { { QStringLiteral("frame"), 9999 } }));

    // The API must not be able to put the playhead outside the media.
    QCOMPARE(fixture.timeline.currentFrame(), qint64(99));
}

void TestApiCommands::stepsForwardAndBackward()
{
    Fixture fixture;
    fixture.dispatcher.dispatch(
        request(QStringLiteral("seek_frame"), { { QStringLiteral("frame"), 10 } }));

    fixture.dispatcher.dispatch(request(QStringLiteral("step_forward")));
    QCOMPARE(fixture.timeline.currentFrame(), qint64(11));

    fixture.dispatcher.dispatch(request(QStringLiteral("step_backward")));
    QCOMPARE(fixture.timeline.currentFrame(), qint64(10));
}

void TestApiCommands::togglesLoop()
{
    Fixture fixture;
    QVERIFY(!fixture.playback.isLoopEnabled());

    const ApiResponse response = fixture.dispatcher.dispatch(
        request(QStringLiteral("set_loop_enabled"), { { QStringLiteral("enabled"), true } }));

    QVERIFY(response.ok);
    QVERIFY(fixture.playback.isLoopEnabled());
}

void TestApiCommands::setsLoopRange()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(
        request(QStringLiteral("set_loop_range"),
                { { QStringLiteral("start"), 10 }, { QStringLiteral("end"), 20 } }));

    QVERIFY(response.ok);
    QVERIFY(fixture.timeline.playbackRange().enabled);
    QCOMPARE(fixture.timeline.playbackRange().startFrame, qint64(10));
    QCOMPARE(fixture.timeline.playbackRange().endFrame, qint64(20));
}

void TestApiCommands::addsBookmarkAtCurrentFrameByDefault()
{
    Fixture fixture;
    fixture.dispatcher.dispatch(
        request(QStringLiteral("seek_frame"), { { QStringLiteral("frame"), 33 } }));

    const ApiResponse response = fixture.dispatcher.dispatch(request(QStringLiteral("add_bookmark")));

    QVERIFY(response.ok);
    QCOMPARE(fixture.timeline.bookmarks().size(), qsizetype(1));
    QCOMPARE(fixture.timeline.bookmarks().at(0).frame, qint64(33));
}

void TestApiCommands::addsBookmarkAtGivenFrame()
{
    Fixture fixture;
    const ApiResponse response = fixture.dispatcher.dispatch(
        request(QStringLiteral("add_bookmark"),
                { { QStringLiteral("frame"), 12 },
                  { QStringLiteral("name"), QStringLiteral("contact") },
                  { QStringLiteral("note"), QStringLiteral("foot slides") },
                  { QStringLiteral("color"), 3 } }));

    QVERIFY(response.ok);
    const auto& bookmark = fixture.timeline.bookmarks().at(0);
    QCOMPARE(bookmark.frame, qint64(12));
    QCOMPARE(bookmark.name, QStringLiteral("contact"));
    QCOMPARE(bookmark.note, QStringLiteral("foot slides"));
    QCOMPARE(bookmark.colorIndex, 3);
}

void TestApiCommands::reportsUnimplementedCommandsExplicitly()
{
    Fixture fixture;
    // These are advertised by list_commands, so a client must be able to tell
    // "not implemented" from "not recognised".
    const QStringList names{
        QStringLiteral("open_media"),
        QStringLiteral("load_compare_a"),
        QStringLiteral("load_compare_b"),
    };

    for (const QString& name : names) {
        const ApiResponse response = fixture.dispatcher.dispatch(request(name));
        QVERIFY(!response.ok);
        QVERIFY2(response.error.contains(QStringLiteral("not implemented")), qPrintable(name));
        QVERIFY2(!response.error.contains(QStringLiteral("unknown")), qPrintable(name));
    }
}

void TestApiCommands::responseSerialisesWithId()
{
    const QJsonObject json = ApiResponse::success({ { QStringLiteral("a"), 1 } }).toJson(7);
    QCOMPARE(json.value(QStringLiteral("id")).toInt(), 7);
    QVERIFY(json.value(QStringLiteral("ok")).toBool());
    QVERIFY(json.contains(QStringLiteral("result")));

    const QJsonObject failure = ApiResponse::failure(QStringLiteral("boom")).toJson(8);
    QVERIFY(!failure.value(QStringLiteral("ok")).toBool());
    QCOMPARE(failure.value(QStringLiteral("error")).toString(), QStringLiteral("boom"));
    QVERIFY(!failure.contains(QStringLiteral("result")));
}

QTEST_GUILESS_MAIN(TestApiCommands)
#include "tst_apicommands.moc"
