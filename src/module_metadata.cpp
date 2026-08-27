#include "module_metadata.h"
#include <QPluginLoader>
#include <QJsonArray>
#include <QJsonDocument>

namespace ModuleLib {

std::optional<ModuleMetadata> ModuleMetadata::fromPath(const QString& pluginPath) {
    QPluginLoader loader(pluginPath);
    
    QJsonObject metadata = loader.metaData();
    if (metadata.isEmpty()) {
        qWarning() << "ModuleMetadata: No metadata found for plugin:" << pluginPath;
        return std::nullopt;
    }
    
    return fromJson(metadata);
}

std::optional<ModuleMetadata> ModuleMetadata::fromPath(const std::string& pluginPath) {
    return fromPath(QString::fromStdString(pluginPath));
}

std::optional<ModuleMetadata> ModuleMetadata::fromJson(const QJsonObject& json) {
    QJsonObject customMetadata = json.value("MetaData").toObject();
    if (customMetadata.isEmpty()) {
        qWarning() << "ModuleMetadata: No custom metadata (MetaData section) found";
        return std::nullopt;
    }
    
    ModuleMetadata result = fromCustomMetadata(customMetadata);
    if (!result.isValid()) {
        return std::nullopt;
    }
    
    return result;
}

ModuleMetadata ModuleMetadata::fromCustomMetadata(const QJsonObject& customMetadata) {
    ModuleMetadata result;
    
    result.name = customMetadata.value("name").toString();
    result.displayName = customMetadata.value("display_name").toString();
    result.version = customMetadata.value("version").toString();
    result.description = customMetadata.value("description").toString();
    result.author = customMetadata.value("author").toString();
    result.type = customMetadata.value("type").toString();
    result.rawMetadata = customMetadata;
    result.rawMetadataJson = QJsonDocument(customMetadata)
                                 .toJson(QJsonDocument::Compact)
                                 .toStdString();
    
    // A dependency entry is either a bare name or an object holding that name
    // alongside the constraints it is resolved by (version range, signer DID).
    // Both forms declare the same edge; the bare form simply constrains nothing.
    QJsonArray depsArray = customMetadata.value("dependencies").toArray();
    for (const QJsonValue& dep : depsArray) {
        ModuleDependency entry;
        if (dep.isObject()) {
            const QJsonObject obj = dep.toObject();
            entry.name = obj.value("name").toString().toStdString();
            entry.versionRange = obj.value("version").toString().toStdString();
            entry.signer = obj.value("signer").toString().toStdString();
        } else {
            entry.name = dep.toString().toStdString();
        }
        if (!entry.name.empty()) {
            result.dependencies.push_back(std::move(entry));
        }
    }
    
    return result;
}

QStringList ModuleMetadata::dependencyNames() const {
    QStringList names;
    names.reserve(static_cast<int>(dependencies.size()));
    for (const ModuleDependency& dep : dependencies) {
        names.append(QString::fromStdString(dep.name));
    }
    return names;
}

} // namespace ModuleLib
