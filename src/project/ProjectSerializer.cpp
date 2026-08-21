#include "project/ProjectSerializer.h"
#include "media/MediaSource.h"
#include "project/Project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace atk::project {
namespace {
constexpr int kVersion = 1;

QString storedMediaPath(const QString& mediaPath, const QString& projectPath)
{
    const QDir base(QFileInfo(projectPath).absolutePath());
    const QString relative = QDir::cleanPath(base.relativeFilePath(mediaPath));
    return relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative)
        ? QDir::cleanPath(mediaPath) : relative;
}

QJsonObject bookmarkJson(const timeline::Bookmark& b)
{
    return {{QStringLiteral("id"), QString::number(b.id)},
            {QStringLiteral("type"), b.isRange() ? QStringLiteral("range") : QStringLiteral("point")},
            {QStringLiteral("startFrame"), static_cast<double>(b.frame)},
            {QStringLiteral("endFrame"), static_cast<double>(b.endFrame)},
            {QStringLiteral("mediaTimeUs"), static_cast<double>(b.mediaTimeUs)},
            {QStringLiteral("name"), b.name}, {QStringLiteral("note"), b.note},
            {QStringLiteral("colorIndex"), b.colorIndex}};
}

bool integer(const QJsonValue& value, qint64* out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    const qint64 converted = static_cast<qint64>(number);
    if (static_cast<double>(converted) != number) return false;
    *out = converted;
    return true;
}
}

QString ProjectSerializer::fileExtension() { return QStringLiteral("atkproj"); }
QString ProjectSerializer::fileDialogFilter()
{
    return QCoreApplication::translate("ProjectSerializer", "ATK Player Project (*.atkproj)");
}

SerializerResult ProjectSerializer::save(const Project& project, const QString& filePath)
{
    if (filePath.isEmpty()) return SerializerResult::failure(QStringLiteral("No project path was provided."));
    QJsonArray sources;
    for (const SourceEntry& entry : project.entries()) {
        if (!entry.source) continue;
        QJsonArray bookmarks;
        for (const auto& bookmark : entry.bookmarks) bookmarks.append(bookmarkJson(bookmark));
        const auto& range = entry.playbackRange;
        QJsonObject review{{QStringLiteral("playbackRange"), QJsonObject{
            {QStringLiteral("startFrame"), static_cast<double>(range.startFrame)},
            {QStringLiteral("endFrame"), static_cast<double>(range.endFrame)},
            {QStringLiteral("enabled"), range.enabled}}},
            {QStringLiteral("bookmarks"), bookmarks}};
        sources.append(QJsonObject{{QStringLiteral("id"), entry.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("path"), storedMediaPath(entry.source->filePath(), filePath)},
            {QStringLiteral("displayName"), entry.displayName.isEmpty() ? entry.source->displayName() : entry.displayName},
            {QStringLiteral("frameOffset"), static_cast<double>(entry.frameOffset)},
            {QStringLiteral("review"), review}});
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("ATKProject")},
        {QStringLiteral("version"), kVersion}, {QStringLiteral("name"), project.name()},
        {QStringLiteral("currentSourceId"), project.currentSourceId().toString(QUuid::WithoutBraces)},
        {QStringLiteral("sources"), sources}};
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return SerializerResult::failure(file.errorString());
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
        return SerializerResult::failure(file.errorString());
    return SerializerResult::success();
}

SerializerResult ProjectSerializer::load(Project& project, const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return SerializerResult::failure(file.errorString());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return SerializerResult::failure(QStringLiteral("Invalid project JSON: %1").arg(parseError.errorString()));
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("ATKProject"))
        return SerializerResult::failure(QStringLiteral("This is not an ATK Player project."));
    if (!root.value(QStringLiteral("version")).isDouble())
        return SerializerResult::failure(QStringLiteral("The project version is missing."));
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version != kVersion)
        return SerializerResult::failure(version > kVersion
            ? QStringLiteral("This project was created by a newer version of ATK Player.")
            : QStringLiteral("This project version is not supported."));
    if (!root.value(QStringLiteral("sources")).isArray())
        return SerializerResult::failure(QStringLiteral("The project source list is invalid."));

    QVector<SourceEntry> entries;
    const QDir base(QFileInfo(filePath).absolutePath());
    for (const QJsonValue& value : root.value(QStringLiteral("sources")).toArray()) {
        if (!value.isObject()) return SerializerResult::failure(QStringLiteral("A project source is invalid."));
        const QJsonObject object = value.toObject();
        const QUuid id(object.value(QStringLiteral("id")).toString());
        const QString storedPath = object.value(QStringLiteral("path")).toString();
        if (id.isNull() || storedPath.isEmpty())
            return SerializerResult::failure(QStringLiteral("A project source has no valid ID or path."));
        const QString resolved = QDir::isAbsolutePath(storedPath)
            ? QDir::cleanPath(storedPath) : QDir::cleanPath(base.absoluteFilePath(storedPath));
        SourceEntry entry;
        entry.id = id; entry.storedPath = storedPath;
        entry.displayName = object.value(QStringLiteral("displayName")).toString();
        entry.source = std::make_shared<media::MediaSource>(resolved);
        entry.missing = !QFileInfo::exists(resolved);
        qint64 offset = 0;
        if (integer(object.value(QStringLiteral("frameOffset")), &offset)) entry.frameOffset = offset;
        const QJsonObject review = object.value(QStringLiteral("review")).toObject();
        const QJsonObject range = review.value(QStringLiteral("playbackRange")).toObject();
        qint64 start = 0, end = 0;
        if (integer(range.value(QStringLiteral("startFrame")), &start)
            && integer(range.value(QStringLiteral("endFrame")), &end) && start >= 0 && end >= start)
            entry.playbackRange = {start, end, range.value(QStringLiteral("enabled")).toBool()};
        for (const QJsonValue& bookmarkValue : review.value(QStringLiteral("bookmarks")).toArray()) {
            const QJsonObject b = bookmarkValue.toObject(); qint64 frame = 0, endFrame = 0, mediaTime = 0;
            bool idOk = false; const quint64 bookmarkId = b.value(QStringLiteral("id")).toString().toULongLong(&idOk);
            if (!idOk || !integer(b.value(QStringLiteral("startFrame")), &frame)
                || !integer(b.value(QStringLiteral("endFrame")), &endFrame) || frame < 0 || endFrame < frame)
                return SerializerResult::failure(QStringLiteral("A bookmark has invalid frame data."));
            integer(b.value(QStringLiteral("mediaTimeUs")), &mediaTime);
            timeline::Bookmark bookmark; bookmark.id = bookmarkId; bookmark.frame = frame; bookmark.endFrame = endFrame;
            bookmark.type = b.value(QStringLiteral("type")).toString() == QStringLiteral("range")
                ? timeline::BookmarkType::Range : timeline::BookmarkType::Point;
            bookmark.mediaTimeUs = mediaTime; bookmark.name = b.value(QStringLiteral("name")).toString();
            bookmark.note = b.value(QStringLiteral("note")).toString();
            bookmark.colorIndex = b.value(QStringLiteral("colorIndex")).toInt(timeline::Bookmark::kNoColor);
            entry.bookmarks.append(bookmark);
        }
        entries.append(std::move(entry));
    }
    project.replace(root.value(QStringLiteral("name")).toString(QStringLiteral("Untitled")),
                    QFileInfo(filePath).absoluteFilePath(), std::move(entries),
                    QUuid(root.value(QStringLiteral("currentSourceId")).toString()));
    return SerializerResult::success();
}
} // namespace atk::project
