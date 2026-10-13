#include "project/ProjectSerializer.h"
#include "annotation/AnnotationTypes.h"
#include "annotation/BrushTip.h"
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
#include <QSet>

#include <algorithm>
#include <limits>

namespace atk::project {
namespace {
// Version 2 marks a project carrying drawings or notes. Older builds refuse
// it ("created by a newer version") instead of opening it, dropping the
// drawings they do not know, and saving over it. Projects without drawings
// stay version 1, so they still open in older builds.
constexpr int kVersion = 1;
constexpr int kAnnotatedVersion = 2;
// Version 3 marks a project carrying review metadata (clip statuses), for
// the same reason: a version 2 reader would drop it and save over it.
constexpr int kReviewVersion = 3;
/// Ids above this are refused on load, so allocating the next id can never
/// wrap round to the reserved 0.
constexpr quint64 kMaxAnnotationId = quint64(1) << 53;

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

/// A still image's hold is saved with the source, so bookmarks and the review
/// range keep the extent they were made against even if the application's
/// default hold later changes.
QJsonObject stillJson(const media::ImageSourceOptions& still)
{
    return {{QStringLiteral("holdFrames"), static_cast<double>(still.holdFrames)},
            {QStringLiteral("frameRate"), QJsonObject{
                {QStringLiteral("numerator"), still.frameRate.numerator},
                {QStringLiteral("denominator"), still.frameRate.denominator}}}};
}

QJsonObject rateJson(const media::FrameRate& rate)
{
    return {{QStringLiteral("numerator"), rate.numerator},
            {QStringLiteral("denominator"), rate.denominator}};
}

bool readRate(const QJsonValue& value, media::FrameRate* out)
{
    const QJsonObject rate = value.toObject();
    qint64 numerator = 0, denominator = 0;
    if (!integer(rate.value(QStringLiteral("numerator")), &numerator)
        || !integer(rate.value(QStringLiteral("denominator")), &denominator)
        || numerator <= 0 || denominator <= 0
        || numerator > std::numeric_limits<int>::max()
        || denominator > std::numeric_limits<int>::max())
        return false;
    *out = {static_cast<int>(numerator), static_cast<int>(denominator)};
    return true;
}

/// An image sequence is stored as one path -- the printf-style pattern -- plus
/// its inclusive source frame range and its own rate, so the extent and
/// timing bookmarks were made against survive a preference change.
QJsonObject sequenceJson(const media::ImageSourceOptions& sequence)
{
    return {{QStringLiteral("firstFrame"), static_cast<double>(sequence.sequenceFirst)},
            {QStringLiteral("lastFrame"), static_cast<double>(sequence.sequenceLast)},
            {QStringLiteral("frameRate"), rateJson(sequence.frameRate)}};
}

bool readSequence(const QJsonValue& value, media::ImageSourceOptions* out)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    media::ImageSourceOptions sequence;
    qint64 first = 0, last = 0;
    if (!integer(object.value(QStringLiteral("firstFrame")), &first)
        || !integer(object.value(QStringLiteral("lastFrame")), &last)
        || !readRate(object.value(QStringLiteral("frameRate")), &sequence.frameRate))
        return false;
    sequence.sequenceFirst = first;
    sequence.sequenceLast = last;
    if (!sequence.isValid()) return false;
    *out = sequence;
    return true;
}

bool readStill(const QJsonValue& value, media::ImageSourceOptions* out)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    const QJsonObject rate = object.value(QStringLiteral("frameRate")).toObject();
    qint64 hold = 0, numerator = 0, denominator = 0;
    if (!integer(object.value(QStringLiteral("holdFrames")), &hold)
        || !integer(rate.value(QStringLiteral("numerator")), &numerator)
        || !integer(rate.value(QStringLiteral("denominator")), &denominator)
        || numerator <= 0 || denominator <= 0
        || numerator > std::numeric_limits<int>::max()
        || denominator > std::numeric_limits<int>::max())
        return false;
    media::ImageSourceOptions still;
    still.holdFrames = hold;
    still.frameRate = {static_cast<int>(numerator), static_cast<int>(denominator)};
    if (!still.isValid()) return false;
    *out = still;
    return true;
}

QJsonObject annotationItemJson(const annotation::AnnotationItem& item)
{
    QJsonArray points;
    for (const annotation::StrokePoint& p : item.points) {
        QJsonArray point{static_cast<double>(p.x), static_cast<double>(p.y),
                         static_cast<double>(p.pressure)};
        // Tilt is rare; keep ordinary mouse strokes compact.
        if (p.tiltX != 0.0f || p.tiltY != 0.0f) {
            point.append(static_cast<double>(p.tiltX));
            point.append(static_cast<double>(p.tiltY));
        }
        points.append(point);
    }
    QJsonObject object{{QStringLiteral("id"), QString::number(item.id)},
                       {QStringLiteral("tool"), annotation::toolName(item.tool)},
                       {QStringLiteral("color"), item.color.name(QColor::HexArgb)},
                       {QStringLiteral("size"), item.size},
                       {QStringLiteral("opacity"), item.opacity},
                       {QStringLiteral("seed"), static_cast<double>(item.textureSeed)},
                       {QStringLiteral("points"), points}};
    if (!item.text.isEmpty()) object.insert(QStringLiteral("text"), item.text);
    if (!item.brushId.isEmpty()) object.insert(QStringLiteral("brush"), item.brushId);
    return object;
}

QJsonArray annotationsJson(const annotation::SourceAnnotations& annotations)
{
    QJsonArray frames;
    for (const annotation::FrameAnnotation& frame : annotations.frames()) {
        QJsonArray items;
        for (const annotation::AnnotationItem& item : frame.items) items.append(annotationItemJson(item));
        QJsonObject object{{QStringLiteral("frame"), static_cast<double>(frame.frame)},
                           {QStringLiteral("mediaTimeUs"), static_cast<double>(frame.mediaTimeUs)},
                           {QStringLiteral("items"), items}};
        if (!frame.notes.isEmpty()) {
            QJsonArray notes;
            for (const annotation::FrameNote& note : frame.notes)
                notes.append(QJsonObject{{QStringLiteral("id"), QString::number(note.id)},
                                         {QStringLiteral("text"), note.text},
                                         {QStringLiteral("author"), note.author},
                                         {QStringLiteral("createdMsUtc"), static_cast<double>(note.createdMsUtc)},
                                         {QStringLiteral("resolved"), note.resolved}});
            object.insert(QStringLiteral("notes"), notes);
        }
        frames.append(object);
    }
    return frames;
}

bool readAnnotationItem(const QJsonValue& value, annotation::AnnotationItem* out)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    annotation::AnnotationItem item;
    bool idOk = false;
    item.id = object.value(QStringLiteral("id")).toString().toULongLong(&idOk);
    if (!idOk || item.id == 0 || item.id > kMaxAnnotationId) return false;
    if (!annotation::toolFromName(object.value(QStringLiteral("tool")).toString(), &item.tool))
        return false;
    item.color = QColor(object.value(QStringLiteral("color")).toString());
    if (!object.value(QStringLiteral("size")).isDouble()
        || !object.value(QStringLiteral("opacity")).isDouble())
        return false;
    item.size = object.value(QStringLiteral("size")).toDouble();
    item.opacity = object.value(QStringLiteral("opacity")).toDouble();
    qint64 seed = 0;
    if (!integer(object.value(QStringLiteral("seed")), &seed) || seed < 0
        || seed > std::numeric_limits<quint32>::max())
        return false;
    item.textureSeed = static_cast<quint32>(seed);
    item.text = object.value(QStringLiteral("text")).toString();
    item.brushId = object.value(QStringLiteral("brush")).toString();
    if (!object.value(QStringLiteral("points")).isArray()) return false;
    const QJsonArray points = object.value(QStringLiteral("points")).toArray();
    if (points.size() > annotation::AnnotationLimits::kMaxPointsPerItem) return false;
    item.points.reserve(int(points.size()));
    for (const QJsonValue& pointValue : points) {
        const QJsonArray p = pointValue.toArray();
        if (p.size() != 3 && p.size() != 5) return false;
        for (const QJsonValue& number : p)
            if (!number.isDouble()) return false;
        annotation::StrokePoint point;
        point.x = static_cast<float>(p.at(0).toDouble());
        point.y = static_cast<float>(p.at(1).toDouble());
        point.pressure = static_cast<float>(p.at(2).toDouble());
        if (p.size() == 5) {
            point.tiltX = static_cast<float>(p.at(3).toDouble());
            point.tiltY = static_cast<float>(p.at(4).toDouble());
        }
        item.points.append(point);
    }
    if (!annotation::isValidItem(item)) return false;
    *out = std::move(item);
    return true;
}

/// Annotations are additive review state, like bookmarks. Malformed data
/// rejects the project rather than silently dropping someone's drawings.
bool readAnnotations(const QJsonValue& value, annotation::SourceAnnotations* out)
{
    if (value.isUndefined()) return true; // a project from before drawings
    // The writer never stores null: an explicit null is damage, not "none".
    if (!value.isArray()) return false;
    const QJsonArray frames = value.toArray();
    if (frames.size() > annotation::AnnotationLimits::kMaxAnnotatedFrames) return false;
    annotation::SourceAnnotations result;
    QSet<quint64> ids;
    for (const QJsonValue& frameValue : frames) {
        if (!frameValue.isObject()) return false;
        const QJsonObject object = frameValue.toObject();
        annotation::FrameAnnotation frame;
        qint64 index = 0, mediaTime = 0;
        if (!integer(object.value(QStringLiteral("frame")), &index) || index < 0) return false;
        integer(object.value(QStringLiteral("mediaTimeUs")), &mediaTime);
        if (result.hasFrame(index) || !object.value(QStringLiteral("items")).isArray()) return false;
        const QJsonArray items = object.value(QStringLiteral("items")).toArray();
        const QJsonValue notesValue = object.value(QStringLiteral("notes"));
        if (!notesValue.isUndefined() && !notesValue.isArray()) return false;
        const QJsonArray notes = notesValue.toArray();
        if ((items.isEmpty() && notes.isEmpty()) || items.size() > annotation::AnnotationLimits::kMaxItemsPerFrame
            || notes.size() > annotation::AnnotationLimits::kMaxNotesPerFrame)
            return false;
        frame.frame = index;
        frame.mediaTimeUs = mediaTime;
        for (const QJsonValue& itemValue : items) {
            annotation::AnnotationItem item;
            if (!readAnnotationItem(itemValue, &item) || ids.contains(item.id)) return false;
            ids.insert(item.id);
            frame.items.append(std::move(item));
        }
        for (const QJsonValue& noteValue : notes) {
            const QJsonObject n = noteValue.toObject();
            annotation::FrameNote note;
            bool idOk = false;
            note.id = n.value(QStringLiteral("id")).toString().toULongLong(&idOk);
            note.text = n.value(QStringLiteral("text")).toString();
            note.author = n.value(QStringLiteral("author")).toString();
            qint64 created = 0;
            integer(n.value(QStringLiteral("createdMsUtc")), &created);
            note.createdMsUtc = created;
            note.resolved = n.value(QStringLiteral("resolved")).toBool(false);
            if (!noteValue.isObject() || !idOk || note.id == 0 || note.id > kMaxAnnotationId
                || ids.contains(note.id)
                || !annotation::isValidNote(note))
                return false;
            ids.insert(note.id);
            frame.notes.append(std::move(note));
        }
        result.setFrame(std::move(frame));
    }
    *out = std::move(result);
    return true;
}
}

QString ProjectSerializer::fileExtension() { return QStringLiteral("atkproj"); }
QString ProjectSerializer::fileDialogFilter()
{
    return QCoreApplication::translate("ProjectSerializer", "ATK Player Project (*.atkproj)");
}

static SerializerResult saveWithName(const Project& project, const QString& filePath,
                                     const QString& projectName)
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
        // Omitted when empty; when present the project is written as version 2
        // (see kAnnotatedVersion) so older builds cannot strip it.
        if (!entry.annotations.isEmpty())
            review.insert(QStringLiteral("annotations"), annotationsJson(entry.annotations));
        // Likewise omitted at the default; present means version 3.
        if (entry.status != ClipStatus::NotReviewed)
            review.insert(QStringLiteral("status"), clipStatusKey(entry.status));
        QJsonObject source{{QStringLiteral("id"), entry.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("path"), storedMediaPath(entry.source->filePath(), filePath)},
            {QStringLiteral("displayName"), entry.displayName.isEmpty() ? entry.source->displayName() : entry.displayName},
            {QStringLiteral("frameOffset"), static_cast<double>(entry.frameOffset)},
            {QStringLiteral("review"), review}};
        // Additive v1 fields: older readers ignore them, and video sources omit them.
        if (entry.source->isImageSequence())
            source.insert(QStringLiteral("sequence"), sequenceJson(entry.source->imageOptions()));
        else if (entry.source->isStillImage())
            source.insert(QStringLiteral("still"), stillJson(entry.source->imageOptions()));
        sources.append(source);
    }
    const bool annotated = std::any_of(project.entries().cbegin(), project.entries().cend(),
                                       [](const SourceEntry& entry) { return !entry.annotations.isEmpty(); });
    const bool reviewed = std::any_of(project.entries().cbegin(), project.entries().cend(),
                                      [](const SourceEntry& entry) { return entry.status != ClipStatus::NotReviewed; });
    const int version = reviewed ? kReviewVersion : annotated ? kAnnotatedVersion : kVersion;
    QJsonObject root{{QStringLiteral("format"), QStringLiteral("ATKProject")},
        {QStringLiteral("version"), version}, {QStringLiteral("name"), projectName},
        {QStringLiteral("currentSourceId"), project.currentSourceId().toString(QUuid::WithoutBraces)},
        {QStringLiteral("sources"), sources}};
    // Imported brush tips used by any drawing travel with the project, so it
    // renders the same on a machine that never imported the brush set.
    QSet<QString> usedTips;
    for (const SourceEntry& entry : project.entries())
        for (const annotation::FrameAnnotation& frame : entry.annotations.frames())
            for (const annotation::AnnotationItem& item : frame.items)
                if (!item.brushId.isEmpty()) usedTips.insert(item.brushId);
    if (!usedTips.isEmpty()) {
        QJsonArray tips;
        QStringList ids(usedTips.cbegin(), usedTips.cend());
        ids.sort();
        for (const QString& id : ids) {
            const annotation::BrushTip tip = annotation::BrushTipRegistry::instance().find(id);
            if (!tip.isValid()) continue; // already missing; drawings fall back to a round brush
            tips.append(QJsonObject{{QStringLiteral("id"), tip.id}, {QStringLiteral("name"), tip.name},
                                    {QStringLiteral("spacing"), tip.spacing}, {QStringLiteral("angle"), tip.angle},
                                    {QStringLiteral("diameter"), tip.diameter},
                                    {QStringLiteral("png"), QString::fromLatin1(tip.maskPng().toBase64())}});
        }
        root.insert(QStringLiteral("brushTips"), tips); // additive v1 field
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return SerializerResult::failure(file.errorString());
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
        return SerializerResult::failure(file.errorString());
    return SerializerResult::success();
}

SerializerResult ProjectSerializer::save(const Project& project, const QString& filePath)
{
    return saveWithName(project, filePath, project.name());
}

SerializerResult ProjectSerializer::saveAs(const Project& project, const QString& filePath)
{
    return saveWithName(project, filePath, QFileInfo(filePath).completeBaseName());
}

SerializerResult ProjectSerializer::load(Project& project, const QString& filePath,
                                         const media::ImageSourceOptions& defaultStill)
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
    if (version != kVersion && version != kAnnotatedVersion && version != kReviewVersion)
        return SerializerResult::failure(version > kReviewVersion
            ? QStringLiteral("This project was created by a newer version of ATK Player.")
            : QStringLiteral("This project version is not supported."));
    if (!root.value(QStringLiteral("sources")).isArray())
        return SerializerResult::failure(QStringLiteral("The project source list is invalid."));

    // Embedded brush tips. A bad tip is skipped (its strokes fall back to a
    // round brush) rather than rejecting the whole project.
    QVector<annotation::BrushTip> embeddedTips;
    for (const QJsonValue& value : root.value(QStringLiteral("brushTips")).toArray()) {
        const QJsonObject t = value.toObject();
        annotation::BrushTip tip;
        tip.id = t.value(QStringLiteral("id")).toString().left(200);
        tip.name = t.value(QStringLiteral("name")).toString().left(200);
        tip.spacing = std::clamp(t.value(QStringLiteral("spacing")).toDouble(0.25), 0.01, 10.0);
        tip.angle = t.value(QStringLiteral("angle")).toDouble(0.0);
        tip.diameter = t.value(QStringLiteral("diameter")).toInt(0);
        const QByteArray png = QByteArray::fromBase64(t.value(QStringLiteral("png")).toString().toLatin1());
        if (png.size() <= 32 * 1024 * 1024) tip.mask = annotation::BrushTip::maskFromPng(png);
        if (tip.isValid()) embeddedTips.append(tip);
    }

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
        if (object.contains(QStringLiteral("sequence"))) {
            media::ImageSourceOptions sequence;
            if (!media::isStillImagePath(resolved)
                || !readSequence(object.value(QStringLiteral("sequence")), &sequence))
                return SerializerResult::failure(QStringLiteral("An image sequence source has an invalid frame range or rate."));
            entry.source->setImageOptions(sequence);
        } else if (entry.source->isStillImage()) {
            media::ImageSourceOptions still = defaultStill.normalized();
            if (object.contains(QStringLiteral("still"))
                && !readStill(object.value(QStringLiteral("still")), &still))
                return SerializerResult::failure(QStringLiteral("A still image source has an invalid hold."));
            entry.source->setImageOptions(still);
        }
        entry.missing = !entry.source->exists();
        entry.availability = entry.missing ? SourceAvailability::Missing : SourceAvailability::Unknown;
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
        entry.status = clipStatusFromKey(review.value(QStringLiteral("status")).toString());
        if (!readAnnotations(review.value(QStringLiteral("annotations")), &entry.annotations))
            return SerializerResult::failure(QStringLiteral("A source has invalid annotation data."));
        entries.append(std::move(entry));
    }
    for (const annotation::BrushTip& tip : embeddedTips) {
        // A tip already registered (from the library) wins; ids are content-derived or UUIDs.
        if (!annotation::BrushTipRegistry::instance().contains(tip.id))
            annotation::BrushTipRegistry::instance().add(tip);
    }
    project.replace(root.value(QStringLiteral("name")).toString(QStringLiteral("Untitled")),
                    QFileInfo(filePath).absoluteFilePath(), std::move(entries),
                    QUuid(root.value(QStringLiteral("currentSourceId")).toString()));
    return SerializerResult::success();
}
} // namespace atk::project
