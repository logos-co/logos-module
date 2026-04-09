#ifndef INSTANCE_PERSISTENCE_H
#define INSTANCE_PERSISTENCE_H

#include <QString>

/**
 * @brief Utilities for resolving per-module-instance persistence directories.
 *
 * Each module instance gets a unique ID and a dedicated directory on disk
 * for storing persistent state, structured as:
 *   {basePath}/{moduleName}/{instanceId}
 */
namespace ModuleLib {
namespace InstancePersistence {

/**
 * @brief Controls how resolveInstance() picks or creates an instance ID.
 */
enum class ResolveMode {
    /// Use the first existing instance directory found for the module.
    /// If none exist, create a new one.
    ReuseOrCreate,

    /// Always generate a fresh instance ID, ignoring any existing directories.
    AlwaysCreate,

    /// Use the explicit instance ID passed via the explicitInstanceId parameter.
    /// The explicitInstanceId parameter must be non-empty when this mode is used.
    UseExplicit
};

struct InstanceInfo {
    QString instanceId;
    QString persistencePath;  // full path: basePath/moduleName/instanceId
};

/**
 * @brief Resolve (or create) an instance directory for the given module.
 *
 * @param basePath           Root persistence directory (e.g. ~/.logoscore/data)
 * @param moduleName         Name of the module (must be a simple name, no path separators or "..")
 * @param mode               How to resolve the instance (see ResolveMode)
 * @param explicitInstanceId Instance ID to use when mode is UseExplicit
 *                           (must be a simple name, no path separators or "..")
 * @return InstanceInfo with the resolved ID and full path.
 *         Returns empty strings if inputs are invalid or directory creation fails.
 *
 * The directory is created on disk (mkpath) before returning.
 */
InstanceInfo resolveInstance(const QString& basePath,
                             const QString& moduleName,
                             ResolveMode mode = ResolveMode::ReuseOrCreate,
                             const QString& explicitInstanceId = QString());

}  // namespace InstancePersistence
}  // namespace ModuleLib

#endif  // INSTANCE_PERSISTENCE_H
