#ifndef MODULE_LIB_H
#define MODULE_LIB_H

/**
 * @file module_lib.h
 * @brief Convenience header that includes all module_lib components.
 * 
 * The module_lib library provides an abstraction layer over Qt's plugin system,
 * allowing for potential future replacement with a different backend while
 * keeping consumer code unchanged.
 * 
 * Main components:
 * - ModuleMetadata: Plugin metadata extraction and storage
 * - LogosModule: Plugin loading, lifecycle management, and runtime introspection
 * 
 * Example usage:
 * @code
 * #include <module_lib/module_lib.h>
 * 
 * using namespace ModuleLib;
 * 
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

#include "module_metadata.h"
#include "logos_module.h"

#endif // MODULE_LIB_H
