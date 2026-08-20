#include "project/ProjectSerializer.h"

#include "core/Logging.h"
#include "project/Project.h"

#include <QCoreApplication>

namespace atk::project {
namespace {

QString notImplementedMessage()
{
    return QCoreApplication::translate(
        "ProjectSerializer",
        "Saving and loading projects is not implemented yet (planned for milestone M3).");
}

} // namespace

QString ProjectSerializer::fileExtension()
{
    return QStringLiteral("atkproj");
}

QString ProjectSerializer::fileDialogFilter()
{
    return QCoreApplication::translate("ProjectSerializer",
                                       "ATK Player Project (*.atkproj)");
}

SerializerResult ProjectSerializer::save(const Project& project, const QString& filePath)
{
    Q_UNUSED(project);
    qCWarning(log::project).noquote() << "Save requested for" << filePath << "-- not implemented";
    return SerializerResult::failure(notImplementedMessage());
}

SerializerResult ProjectSerializer::load(Project& project, const QString& filePath)
{
    Q_UNUSED(project);
    qCWarning(log::project).noquote() << "Load requested for" << filePath << "-- not implemented";
    return SerializerResult::failure(notImplementedMessage());
}

} // namespace atk::project
