#include "review/ReviewLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace atk::review {
namespace {

constexpr int kFormatVersion = 1;

QString absolute(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QJsonObject toJson(const ReviewInfo& r)
{
    QJsonArray previews;
    for (const QString& clip : r.previewClips) previews.append(clip);
    return {{QStringLiteral("id"), r.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("name"), r.name},
            {QStringLiteral("folder"), QDir::fromNativeSeparators(r.folder)},
            {QStringLiteral("project"), QDir::fromNativeSeparators(r.projectPath)},
            {QStringLiteral("includeSubfolders"), r.includeSubfolders},
            {QStringLiteral("starred"), r.starred},
            {QStringLiteral("archived"), r.archived},
            {QStringLiteral("created"), double(r.createdMsUtc)},
            {QStringLiteral("opened"), double(r.openedMsUtc)},
            {QStringLiteral("clipCount"), r.clipCount},
            {QStringLiteral("previews"), previews}};
}

bool fromJson(const QJsonObject& o, ReviewInfo* r)
{
    r->id = QUuid::fromString(o.value(QStringLiteral("id")).toString());
    r->name = o.value(QStringLiteral("name")).toString();
    r->folder = o.value(QStringLiteral("folder")).toString();
    r->projectPath = o.value(QStringLiteral("project")).toString();
    if (r->id.isNull() || r->projectPath.isEmpty()) return false;
    r->includeSubfolders = o.value(QStringLiteral("includeSubfolders")).toBool();
    r->starred = o.value(QStringLiteral("starred")).toBool();
    r->archived = o.value(QStringLiteral("archived")).toBool();
    r->createdMsUtc = qint64(o.value(QStringLiteral("created")).toDouble());
    r->openedMsUtc = qint64(o.value(QStringLiteral("opened")).toDouble());
    r->clipCount = std::max(0, o.value(QStringLiteral("clipCount")).toInt());
    for (const QJsonValue& v : o.value(QStringLiteral("previews")).toArray())
        if (v.isString()) r->previewClips.append(v.toString());
    return true;
}

} // namespace

ReviewLibrary::ReviewLibrary(QString filePath) : m_filePath(std::move(filePath)) {}

bool ReviewLibrary::load(QString* error)
{
    m_reviews.clear();
    QFile file(m_filePath);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("The review library file is not valid JSON.");
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() > kFormatVersion) {
        if (error) *error = QStringLiteral("The review library was written by a newer version of ATK Player.");
        return false;
    }
    for (const QJsonValue& value : root.value(QStringLiteral("reviews")).toArray()) {
        ReviewInfo review;
        if (value.isObject() && fromJson(value.toObject(), &review)) m_reviews.append(review);
    }
    return true;
}

bool ReviewLibrary::save(QString* error) const
{
    QJsonArray reviews;
    for (const ReviewInfo& review : m_reviews) reviews.append(toJson(review));
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("ATKReviewLibrary")},
                           {QStringLiteral("version"), kFormatVersion},
                           {QStringLiteral("reviews"), reviews}};
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

const ReviewInfo* ReviewLibrary::find(const QUuid& id) const
{
    const auto it = std::find_if(m_reviews.cbegin(), m_reviews.cend(), [&](const ReviewInfo& r) { return r.id == id; });
    return it == m_reviews.cend() ? nullptr : &*it;
}

const ReviewInfo* ReviewLibrary::findByProject(const QString& projectPath) const
{
    const QString wanted = absolute(projectPath);
    if (wanted.isEmpty()) return nullptr;
    const auto it = std::find_if(m_reviews.cbegin(), m_reviews.cend(), [&](const ReviewInfo& r) {
        return QString::compare(absolute(r.projectPath), wanted,
#ifdef Q_OS_WIN
                                Qt::CaseInsensitive
#else
                                Qt::CaseSensitive
#endif
                                ) == 0;
    });
    return it == m_reviews.cend() ? nullptr : &*it;
}

QUuid ReviewLibrary::add(ReviewInfo review)
{
    if (const ReviewInfo* existing = findByProject(review.projectPath)) {
        review.id = existing->id;
        update(review);
        return review.id;
    }
    if (review.id.isNull()) review.id = QUuid::createUuid();
    m_reviews.append(std::move(review));
    return m_reviews.constLast().id;
}

bool ReviewLibrary::update(const ReviewInfo& review)
{
    const auto it = std::find_if(m_reviews.begin(), m_reviews.end(), [&](const ReviewInfo& r) { return r.id == review.id; });
    if (it == m_reviews.end()) return false;
    *it = review;
    return true;
}

bool ReviewLibrary::remove(const QUuid& id)
{
    const auto before = m_reviews.size();
    m_reviews.erase(std::remove_if(m_reviews.begin(), m_reviews.end(), [&](const ReviewInfo& r) { return r.id == id; }),
                    m_reviews.end());
    return m_reviews.size() != before;
}

} // namespace atk::review
