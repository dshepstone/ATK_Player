#include "annotation/BrushLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace atk::annotation {
namespace {
/// A folder name that is safe on every platform, derived from the set name.
QString safeFolderName(const QString& name)
{
    QString out;
    for (const QChar c : name) out.append(c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_') ? c : QLatin1Char('_'));
    out = out.left(80);
    return out.isEmpty() ? QStringLiteral("brushes") : out;
}
} // namespace

BrushLibrary::BrushLibrary(QString directory) : m_directory(std::move(directory)) {}

QString BrushLibrary::folderFor(const QString& setName) const
{
    return QDir(m_directory).filePath(safeFolderName(setName));
}

void BrushLibrary::load()
{
    m_sets.clear();
    const QDir root(m_directory);
    for (const QString& folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir dir(root.filePath(folder));
        QFile file(dir.filePath(QStringLiteral("set.json")));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        BrushSet set;
        set.name = json.value(QStringLiteral("name")).toString(folder);
        for (const QJsonValue& value : json.value(QStringLiteral("tips")).toArray()) {
            const QJsonObject t = value.toObject();
            QFile png(dir.filePath(QFileInfo(t.value(QStringLiteral("file")).toString()).fileName()));
            if (!png.open(QIODevice::ReadOnly)) continue;
            BrushTip tip;
            tip.id = t.value(QStringLiteral("id")).toString();
            tip.name = t.value(QStringLiteral("name")).toString();
            tip.spacing = std::clamp(t.value(QStringLiteral("spacing")).toDouble(0.25), 0.01, 10.0);
            tip.angle = t.value(QStringLiteral("angle")).toDouble(0.0);
            tip.diameter = t.value(QStringLiteral("diameter")).toInt(0);
            tip.mask = BrushTip::maskFromPng(png.readAll());
            if (tip.isValid()) set.tips.append(tip);
        }
        if (!set.tips.isEmpty()) m_sets.append(set);
    }
}

bool BrushLibrary::addSet(const BrushSet& set, QString* error)
{
    const QString folder = folderFor(set.name);
    QDir dir(folder);
    if (dir.exists()) dir.removeRecursively();
    if (!QDir().mkpath(folder)) {
        if (error) *error = QStringLiteral("Could not create %1.").arg(folder);
        return false;
    }
    QJsonArray tips;
    int index = 0;
    for (const BrushTip& tip : set.tips) {
        const QString fileName = QStringLiteral("tip_%1.png").arg(index++, 4, 10, QLatin1Char('0'));
        QSaveFile png(QDir(folder).filePath(fileName));
        if (!png.open(QIODevice::WriteOnly) || png.write(tip.maskPng()) < 0 || !png.commit()) {
            if (error) *error = png.errorString();
            return false;
        }
        tips.append(QJsonObject{{QStringLiteral("id"), tip.id}, {QStringLiteral("name"), tip.name},
                                {QStringLiteral("spacing"), tip.spacing}, {QStringLiteral("angle"), tip.angle},
                                {QStringLiteral("diameter"), tip.diameter}, {QStringLiteral("file"), fileName}});
    }
    QSaveFile json(QDir(folder).filePath(QStringLiteral("set.json")));
    if (!json.open(QIODevice::WriteOnly)
        || json.write(QJsonDocument(QJsonObject{{QStringLiteral("name"), set.name}, {QStringLiteral("tips"), tips}}).toJson()) < 0
        || !json.commit()) {
        if (error) *error = json.errorString();
        return false;
    }
    m_sets.erase(std::remove_if(m_sets.begin(), m_sets.end(), [&](const BrushSet& s) { return s.name == set.name; }),
                 m_sets.end());
    m_sets.append(set);
    return true;
}

bool BrushLibrary::removeSet(const QString& name)
{
    const auto it = std::find_if(m_sets.begin(), m_sets.end(), [&](const BrushSet& s) { return s.name == name; });
    if (it == m_sets.end()) return false;
    m_sets.erase(it);
    QDir(folderFor(name)).removeRecursively();
    return true;
}

BrushTip BrushLibrary::findTip(const QString& id) const
{
    for (const BrushSet& set : m_sets)
        for (const BrushTip& tip : set.tips)
            if (tip.id == id) return tip;
    return {};
}

void BrushLibrary::registerAll() const
{
    for (const BrushSet& set : m_sets)
        for (const BrushTip& tip : set.tips) BrushTipRegistry::instance().add(tip);
}

} // namespace atk::annotation
