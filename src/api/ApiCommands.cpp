#include "api/ApiCommands.h"

#include "core/Logging.h"
#include "playback/PlaybackController.h"
#include "timeline/Bookmark.h"
#include "media/MediaMetadata.h"
#include "media/ffmpeg/FFmpegUtil.h"
#include "timeline/TimelineModel.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <utility>

namespace atk::api {
namespace {

/// Reads an integer parameter, reporting a clear error when it is missing or
/// the wrong type rather than silently defaulting to zero.
bool readInt64(const QJsonObject& params, const QString& key, int64_t& out, QString& error)
{
    const QJsonValue value = params.value(key);
    if (value.isUndefined()) {
        error = QStringLiteral("missing required parameter: %1").arg(key);
        return false;
    }
    if (!value.isDouble()) {
        error = QStringLiteral("parameter %1 must be a number").arg(key);
        return false;
    }
    const double number = value.toDouble();
    constexpr double kInt64Lower = -9223372036854775808.0;
    constexpr double kInt64Upper =  9223372036854775808.0;
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < kInt64Lower || number >= kInt64Upper) {
        error = QStringLiteral("parameter %1 must be a 64-bit integer").arg(key);
        return false;
    }
    out = static_cast<int64_t>(number);
    return true;
}

bool readBool(const QJsonObject& params, const QString& key, bool& out, QString& error)
{
    const QJsonValue value = params.value(key);
    if (value.isUndefined()) {
        error = QStringLiteral("missing required parameter: %1").arg(key);
        return false;
    }
    if (!value.isBool()) {
        error = QStringLiteral("parameter %1 must be a boolean").arg(key);
        return false;
    }
    out = value.toBool();
    return true;
}

QString stateName(playback::PlayerState state)
{
    // Mirrors PlayerState exactly. Reported as lower-case strings so a client
    // never has to know the C++ enumerator names.
    switch (state) {
    case playback::PlayerState::Empty:   return QStringLiteral("empty");
    case playback::PlayerState::Loading: return QStringLiteral("loading");
    case playback::PlayerState::Ready:   return QStringLiteral("ready");
    case playback::PlayerState::Playing: return QStringLiteral("playing");
    case playback::PlayerState::Paused:  return QStringLiteral("paused");
    case playback::PlayerState::Seeking: return QStringLiteral("seeking");
    case playback::PlayerState::Ended:   return QStringLiteral("ended");
    case playback::PlayerState::Error:   return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

} // namespace

ApiResponse ApiResponse::success(QJsonObject result)
{
    ApiResponse response;
    response.ok = true;
    response.result = std::move(result);
    return response;
}

ApiResponse ApiResponse::failure(QString error)
{
    ApiResponse response;
    response.ok = false;
    response.error = std::move(error);
    return response;
}

QJsonObject ApiResponse::toJson(const QJsonValue& requestId) const
{
    QJsonObject object;
    if (!requestId.isUndefined() && !requestId.isNull()) {
        object.insert(QStringLiteral("id"), requestId);
    }
    object.insert(QStringLiteral("ok"), ok);
    if (ok) {
        object.insert(QStringLiteral("result"), result);
    } else {
        object.insert(QStringLiteral("error"), error);
    }
    return object;
}

ApiCommandDispatcher::ApiCommandDispatcher(playback::PlaybackController* playback,
                                           timeline::TimelineModel* timeline,
                                           ApplicationCommandHandler applicationHandler,
                                           ApiInfoProvider apiInfoProvider)
    : m_playback(playback)
    , m_timeline(timeline)
    , m_applicationHandler(std::move(applicationHandler))
    , m_apiInfoProvider(std::move(apiInfoProvider))
{
}

QStringList ApiCommandDispatcher::supportedCommands()
{
    return {
        QStringLiteral("list_commands"),
        QStringLiteral("get_api_info"),
        QStringLiteral("get_status"),
        QStringLiteral("open_media"),
        QStringLiteral("open_project"),
        QStringLiteral("play"),
        QStringLiteral("pause"),
        QStringLiteral("stop"),
        QStringLiteral("seek_frame"),
        QStringLiteral("step_forward"),
        QStringLiteral("step_backward"),
        QStringLiteral("get_current_frame"),
        QStringLiteral("set_loop_enabled"),
        QStringLiteral("set_loop_range"),
        QStringLiteral("clear_loop_range"),
        QStringLiteral("add_bookmark"),
        QStringLiteral("load_compare_a"),
        QStringLiteral("load_compare_b"),
        QStringLiteral("set_compare_offset"),
        QStringLiteral("new_project"),
        QStringLiteral("save_project"),
        QStringLiteral("save_project_as"),
        QStringLiteral("add_media"),
        QStringLiteral("list_sources"),
        QStringLiteral("activate_source"),
        QStringLiteral("set_comparison_enabled"),
        QStringLiteral("set_compare_view"),
        QStringLiteral("set_compare_audio_mode"),
        QStringLiteral("load_external_audio"),
        QStringLiteral("clear_external_audio"),
        QStringLiteral("set_external_audio_offset"),
        QStringLiteral("list_bookmarks"),
        QStringLiteral("export_review"),
        QStringLiteral("export_frame"),
        QStringLiteral("export_image_sequence"),
        QStringLiteral("get_export_status"),
        QStringLiteral("cancel_export"),
        QStringLiteral("show_window"),
    };
}

ApiResponse ApiCommandDispatcher::dispatch(const QJsonObject& request)
{
    if (!m_playback || !m_timeline) {
        return ApiResponse::failure(QStringLiteral("player is not ready"));
    }

    const QString command = request.value(QStringLiteral("command")).toString();
    if (command.isEmpty()) {
        return ApiResponse::failure(QStringLiteral("missing required field: command"));
    }

    const QJsonObject params = request.value(QStringLiteral("params")).toObject();
    const media::MediaMetadata& metadata = m_playback->metadata();
    QString error;
    int64_t frame = 0;

    qCDebug(log::api).noquote() << "dispatch" << command;

    if (command == QLatin1StringView("list_commands")) {
        QJsonArray names;
        const QStringList commandNames = supportedCommands();
        for (const QString& name : commandNames) {
            names.append(name);
        }
        return ApiResponse::success({ { QStringLiteral("commands"), names } });
    }

    if (command == QLatin1StringView("get_api_info")) {
        QJsonObject info = m_apiInfoProvider ? m_apiInfoProvider() : QJsonObject{};
        info.insert(QStringLiteral("protocolVersion"), 1);
        info.insert(QStringLiteral("frameIndexBase"), 0);
        return ApiResponse::success(info);
    }

    if (command == QLatin1StringView("get_status")) {
        QJsonObject result{
            { QStringLiteral("state"),        stateName(m_playback->state()) },
            { QStringLiteral("currentFrame"), static_cast<double>(m_timeline->currentFrame()) },
            { QStringLiteral("displayFrame"), m_timeline->frameCount() > 0
                  ? static_cast<double>(m_timeline->currentFrame() + 1) : 0.0 },
            { QStringLiteral("frameCount"),   static_cast<double>(m_timeline->frameCount()) },
            { QStringLiteral("frameIndexBase"), 0 },
            { QStringLiteral("fpsNumerator"), m_timeline->frameRate().numerator },
            { QStringLiteral("fpsDenominator"), m_timeline->frameRate().denominator },
            { QStringLiteral("fps"),          m_timeline->frameRate().toDouble() },
            { QStringLiteral("loop"),         m_playback->isLoopEnabled() },
            { QStringLiteral("hasMedia"),     m_playback->hasMedia() },
            // True when frameCount/fps describe the Phase 0 placeholder extent
            // rather than an open file. A client must not treat the frame
            // numbers as referring to real media while this is set.
            { QStringLiteral("placeholder"),  m_timeline->isPlaceholder() },

            // M1: real loaded-media detail, so a DCC client can tell what is
            // open and how long it is without a second round trip.
            { QStringLiteral("path"),         metadata.filePath },
            { QStringLiteral("fileName"),     metadata.fileName },
            { QStringLiteral("currentTimeUs"),
              static_cast<double>(atk::media::ffmpeg::frameIndexToMicroseconds(
                  m_timeline->currentFrame(),
                  AVRational{ m_timeline->frameRate().numerator,
                              m_timeline->frameRate().denominator })) },
            { QStringLiteral("durationUs"),   static_cast<double>(metadata.durationUs) },
            // An estimated count must not be presented as fact; a client that
            // seeks to frameCount-1 on an estimate may find it unreachable.
            { QStringLiteral("frameCountIsExact"), metadata.hasExactFrameCount() },
            { QStringLiteral("hasAudio"),     metadata.hasAudio },
        };
        if (m_applicationHandler) {
            const ApiResponse application = m_applicationHandler(command, params);
            if (application.ok) {
                for (auto it = application.result.begin(); it != application.result.end(); ++it)
                    result.insert(it.key(), it.value());
            }
        }
        return ApiResponse::success(result);
    }

    if (command == QLatin1StringView("play")) {
        m_playback->play();
        return ApiResponse::success();
    }

    if (command == QLatin1StringView("pause")) {
        m_playback->pause();
        return ApiResponse::success();
    }

    if (command == QLatin1StringView("stop")) {
        m_playback->stop();
        return ApiResponse::success();
    }

    if (command == QLatin1StringView("seek_frame")) {
        if (!readInt64(params, QStringLiteral("frame"), frame, error)) {
            return ApiResponse::failure(error);
        }
        m_playback->seekFrame(frame);
        return ApiResponse::success({
            { QStringLiteral("accepted"), true },
            { QStringLiteral("targetFrame"), static_cast<double>(m_playback->navigationFrame()) },
        });
    }

    if (command == QLatin1StringView("step_forward")) {
        m_playback->stepForward();
        return ApiResponse::success({
            { QStringLiteral("accepted"), true },
            { QStringLiteral("targetFrame"), static_cast<double>(m_playback->navigationFrame()) },
        });
    }

    if (command == QLatin1StringView("step_backward")) {
        m_playback->stepBackward();
        return ApiResponse::success({
            { QStringLiteral("accepted"), true },
            { QStringLiteral("targetFrame"), static_cast<double>(m_playback->navigationFrame()) },
        });
    }

    if (command == QLatin1StringView("get_current_frame")) {
        return ApiResponse::success({
            { QStringLiteral("currentFrame"), static_cast<double>(m_timeline->currentFrame()) },
            { QStringLiteral("frameCount"),   static_cast<double>(m_timeline->frameCount()) },
        });
    }

    if (command == QLatin1StringView("set_loop_enabled")) {
        bool enabled = false;
        if (!readBool(params, QStringLiteral("enabled"), enabled, error)) {
            return ApiResponse::failure(error);
        }
        m_playback->setLoopEnabled(enabled);
        return ApiResponse::success();
    }

    if (command == QLatin1StringView("set_loop_range")) {
        int64_t start = 0;
        int64_t end = 0;
        if (!readInt64(params, QStringLiteral("start"), start, error)
            || !readInt64(params, QStringLiteral("end"), end, error)) {
            return ApiResponse::failure(error);
        }
        m_playback->setPlaybackRange(start, end);
        return ApiResponse::success();
    }

    if (command == QLatin1StringView("clear_loop_range")) {
        m_playback->clearPlaybackRange();
        return ApiResponse::success();
    }

    if (command == QLatin1StringView("add_bookmark")) {
        timeline::Bookmark bookmark;
        // frame is optional: omitting it bookmarks the current frame, which is
        // what a DCC hotkey binding wants.
        if (params.contains(QStringLiteral("frame"))) {
            if (!readInt64(params, QStringLiteral("frame"), frame, error)) {
                return ApiResponse::failure(error);
            }
            bookmark.frame = frame;
        } else {
            bookmark.frame = m_timeline->currentFrame();
        }
        bookmark.name = params.value(QStringLiteral("name")).toString();
        bookmark.note = params.value(QStringLiteral("note")).toString();
        bookmark.colorIndex = params.value(QStringLiteral("color"))
                                  .toInt(timeline::Bookmark::kNoColor);
        m_timeline->addBookmark(bookmark);
        return ApiResponse::success({
            { QStringLiteral("frame"), static_cast<double>(bookmark.frame) },
        });
    }

    if (supportedCommands().contains(command) && m_applicationHandler)
        return m_applicationHandler(command, params);

    if (supportedCommands().contains(command))
        return ApiResponse::failure(
            QStringLiteral("command %1 requires application services").arg(command));

    return ApiResponse::failure(QStringLiteral("unknown command: %1").arg(command));
}

} // namespace atk::api
