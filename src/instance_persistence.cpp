#include "instance_persistence.h"

#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QDebug>

namespace ModuleLib {
namespace InstancePersistence {

static QString generateInstanceId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

/// Returns true if the name is a safe single path segment (no separators, no "..")
static bool isValidPathSegment(const QString& name)
{
    if (name.isEmpty())
        return false;
    if (name.contains('/') || name.contains('\\'))
        return false;
    if (name == ".." || name == ".")
        return false;
    return true;
}

InstanceInfo resolveInstance(const QString& basePath,
                             const QString& moduleName,
                             ResolveMode mode,
                             const QString& explicitInstanceId)
{
    if (basePath.isEmpty() || moduleName.isEmpty()) {
        qWarning() << "InstancePersistence::resolveInstance: basePath and moduleName must not be empty";
        return {};
    }

    if (!isValidPathSegment(moduleName)) {
        qWarning() << "InstancePersistence::resolveInstance: moduleName contains invalid characters:" << moduleName;
        return {};
    }

    QString instanceId;

    switch (mode) {
    case ResolveMode::UseExplicit:
        if (explicitInstanceId.isEmpty()) {
            qWarning() << "InstancePersistence::resolveInstance: explicitInstanceId must not be empty in UseExplicit mode";
            return {};
        }
        if (!isValidPathSegment(explicitInstanceId)) {
            qWarning() << "InstancePersistence::resolveInstance: explicitInstanceId contains invalid characters:" << explicitInstanceId;
            return {};
        }
        instanceId = explicitInstanceId;
        break;

    case ResolveMode::AlwaysCreate:
        instanceId = generateInstanceId();
        break;

    case ResolveMode::ReuseOrCreate: {
        QDir moduleDir(basePath + "/" + moduleName);
        if (moduleDir.exists()) {
            QStringList entries = moduleDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            if (!entries.isEmpty()) {
                instanceId = entries.first();
            }
        }
        if (instanceId.isEmpty()) {
            instanceId = generateInstanceId();
        }
        break;
    }
    }

    QString persistencePath = basePath + "/" + moduleName + "/" + instanceId;

    // Verify the resolved path is still within basePath (defense in depth)
    QString canonicalBase = QFileInfo(basePath).absoluteFilePath();
    QString canonicalPath = QFileInfo(persistencePath).absoluteFilePath();
    if (!canonicalPath.startsWith(canonicalBase + "/")) {
        qWarning() << "InstancePersistence::resolveInstance: resolved path escapes base directory:" << persistencePath;
        return {};
    }

    QDir dir;
    if (!dir.mkpath(persistencePath)) {
        qWarning() << "InstancePersistence::resolveInstance: failed to create directory:" << persistencePath;
        return {};
    }

    return {instanceId, persistencePath};
}

StdInstanceInfo resolveInstance(const std::string& basePath,
                                const std::string& moduleName,
                                ResolveMode mode,
                                const std::string& explicitInstanceId)
{
    InstanceInfo info = resolveInstance(
        QString::fromStdString(basePath),
        QString::fromStdString(moduleName),
        mode,
        QString::fromStdString(explicitInstanceId));
    return {info.instanceId.toStdString(), info.persistencePath.toStdString()};
}

}  // namespace InstancePersistence
}  // namespace ModuleLib
