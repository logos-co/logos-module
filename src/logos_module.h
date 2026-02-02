#ifndef LOGOS_MODULE_H
#define LOGOS_MODULE_H

#include "module_metadata.h"
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPluginLoader>
#include <memory>
#include <vector>
#include <optional>

namespace ModuleLib {

/**
 * @brief ParameterInfo represents information about a method parameter.
 */
struct ParameterInfo {
    QString name;
    QString type;
    
    QJsonObject toJson() const;
};

/**
 * @brief MethodInfo represents information about a single method.
 */
struct MethodInfo {
    QString name;
    QString signature;
    QString returnType;
    bool isInvokable = false;
    std::vector<ParameterInfo> parameters;
    
    QJsonObject toJson() const;
};

/**
 * @brief LogosModule is an RAII wrapper for a loaded plugin with introspection capabilities.
 * 
 * It manages the lifecycle of a loaded plugin and provides access to
 * the plugin instance, metadata, type-safe casting, and runtime introspection.
 * 
 * Example usage:
 * @code
 * // Load a plugin
 * QString error;
 * LogosModule plugin = LogosModule::loadFromPath("/path/to/plugin.so", &error);
 * if (!plugin.isValid()) {
 *     qCritical() << "Failed to load:" << error;
 *     return;
 * }
 * 
 * // Access metadata
 * qDebug() << "Plugin name:" << plugin.metadata().name;
 * qDebug() << "Plugin version:" << plugin.metadata().version;
 * 
 * // Cast to interface
 * auto* iface = plugin.as<PluginInterface>();
 * if (iface) {
 *     // Use plugin...
 * }
 * 
 * // Introspect methods
 * QJsonArray methods = plugin.getMethodsAsJson();
 * QString className = plugin.getClassName();
 * @endcode
 */
class LogosModule {
public:
    LogosModule();
    ~LogosModule();
    
    // Move semantics (no copying)
    LogosModule(LogosModule&& other) noexcept;
    LogosModule& operator=(LogosModule&& other) noexcept;
    LogosModule(const LogosModule&) = delete;
    LogosModule& operator=(const LogosModule&) = delete;
    
    /**
     * @brief Load a plugin from a file path.
     * 
     * @param pluginPath Path to the plugin file (.so, .dylib, .dll)
     * @param errorString Optional pointer to receive error message on failure
     * @return LogosModule Handle to the loaded plugin (check isValid())
     */
    static LogosModule loadFromPath(const QString& pluginPath, QString* errorString = nullptr);
    
    /**
     * @brief Get all statically linked plugins.
     * 
     * Returns handles to plugins that were linked statically via Q_IMPORT_PLUGIN().
     * 
     * @return std::vector<LogosModule> Vector of handles to static plugins
     */
    static std::vector<LogosModule> getStaticModules();
    
    /**
     * @brief Create a LogosModule from an existing QObject (for static plugins).
     * 
     * @param pluginObject The plugin QObject instance
     * @param metadata Pre-extracted metadata for the plugin
     * @return LogosModule Handle wrapping the existing plugin
     */
    static LogosModule wrapExisting(QObject* pluginObject, const ModuleMetadata& metadata = ModuleMetadata());
    
    /**
     * @brief Extract metadata from a plugin file without loading it.
     * 
     * @param pluginPath Path to the plugin file
     * @return std::optional<ModuleMetadata> The metadata if extraction succeeded
     */
    static std::optional<ModuleMetadata> extractMetadata(const QString& pluginPath);
    
    /**
     * @brief Check if the handle contains a valid loaded plugin
     */
    bool isValid() const;
    
    /**
     * @brief Get the raw QObject instance of the plugin
     */
    QObject* instance() const;
    
    /**
     * @brief Get the metadata for this plugin
     */
    const ModuleMetadata& metadata() const;
    
    /**
     * @brief Get the last error message if loading failed
     */
    QString errorString() const;
    
    /**
     * @brief Cast the plugin instance to a specific interface type.
     * 
     * This wraps qobject_cast to provide type-safe casting.
     * 
     * @tparam T The interface type to cast to
     * @return T* Pointer to the interface, or nullptr if cast fails
     */
    template<typename T>
    T* as() const {
        if (!m_instance) {
            return nullptr;
        }
        return qobject_cast<T*>(m_instance);
    }
    
    /**
     * @brief Unload the plugin and release resources
     */
    void unload();
    
    /**
     * @brief Release ownership of the plugin instance without unloading.
     * 
     * After calling this, the LogosModule no longer manages the plugin lifecycle.
     * The caller is responsible for ensuring proper cleanup.
     * 
     * @return QObject* The plugin instance (ownership transferred to caller)
     */
    QObject* release();
    
    /**
     * @brief Get all methods defined by this plugin.
     * 
     * @param excludeBaseClass If true, excludes methods inherited from QObject
     * @return std::vector<MethodInfo> List of methods defined by the plugin
     */
    std::vector<MethodInfo> getMethods(bool excludeBaseClass = true) const;
    
    /**
     * @brief Get all methods as a QJsonArray.
     * 
     * @param excludeBaseClass If true, excludes methods inherited from QObject
     * @return QJsonArray Array of method information objects
     */
    QJsonArray getMethodsAsJson(bool excludeBaseClass = true) const;
    
    /**
     * @brief Get the class name of the plugin's meta-object.
     * 
     * @return QString The class name
     */
    QString getClassName() const;
    
    /**
     * @brief Check if the plugin has a method with the given name.
     * 
     * @param methodName The method name to look for
     * @return bool True if the method exists
     */
    bool hasMethod(const QString& methodName) const;
    
    /**
     * @brief Get all methods defined by an arbitrary QObject.
     * 
     * @param obj The QObject to introspect
     * @param excludeBaseClass If true, excludes methods inherited from QObject
     * @return std::vector<MethodInfo> List of methods
     */
    static std::vector<MethodInfo> getMethods(QObject* obj, bool excludeBaseClass = true);
    
    /**
     * @brief Get all methods as a QJsonArray for an arbitrary QObject.
     * 
     * @param obj The QObject to introspect
     * @param excludeBaseClass If true, excludes methods inherited from QObject
     * @return QJsonArray Array of method information objects
     */
    static QJsonArray getMethodsAsJson(QObject* obj, bool excludeBaseClass = true);
    
    /**
     * @brief Get the class name of an arbitrary QObject.
     * 
     * @param obj The QObject to introspect
     * @return QString The class name
     */
    static QString getClassName(QObject* obj);
    
    /**
     * @brief Check if an arbitrary QObject has a method with the given name.
     * 
     * @param obj The QObject to check
     * @param methodName The method name to look for
     * @return bool True if the method exists
     */
    static bool hasMethod(QObject* obj, const QString& methodName);

private:
    QPluginLoader* m_loader = nullptr;
    QObject* m_instance = nullptr;
    ModuleMetadata m_metadata;
    QString m_errorString;
    bool m_isStatic = false;
};

} // namespace ModuleLib

#endif // LOGOS_MODULE_H
