#include <QCoreApplication>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <iostream>
#include <vector>
#include <string>

#include "module_loader.h"
#include "module_metadata.h"
#include "module_introspection.h"

using namespace ModuleLib;

// Output streams
QTextStream out(stdout);
QTextStream err(stderr);

// Version
const char* VERSION = "0.1.0";

// Helper functions
void printVersion() {
    out << "lm (Logos Module) version " << VERSION << Qt::endl;
}

void printUsage() {
    out << "lm - Logos Module Inspector\n"
        << "\n"
        << "Usage: lm <command> [options] <plugin-path>\n"
        << "\n"
        << "Commands:\n"
        << "  metadata    Show plugin metadata (name, version, description, etc.)\n"
        << "  methods     Show plugin methods and signatures\n"
        << "\n"
        << "Options:\n"
        << "  --json      Output in JSON format\n"
        << "  --help, -h  Show help information\n"
        << "  --version, -v  Show version information\n"
        << "\n"
        << "Examples:\n"
        << "  lm metadata /path/to/plugin.so\n"
        << "  lm methods /path/to/plugin.so\n"
        << "  lm metadata /path/to/plugin.so --json\n"
        << "  lm methods /path/to/plugin.so --json\n";
}

void printCommandHelp(const QString& command) {
    if (command == "metadata") {
        out << "Usage: lm metadata [options] <plugin-path>\n"
            << "\n"
            << "Show plugin metadata including name, version, description, author,\n"
            << "type, and dependencies.\n"
            << "\n"
            << "Options:\n"
            << "  --json  Output in JSON format\n";
    } else if (command == "methods") {
        out << "Usage: lm methods [options] <plugin-path>\n"
            << "\n"
            << "Show all methods exposed by the plugin via Qt's meta-object system.\n"
            << "Displays method name, signature, return type, and parameters.\n"
            << "\n"
            << "Options:\n"
            << "  --json  Output in JSON format\n";
    }
}

void printMetadataHuman(const ModuleMetadata& metadata) {
    out << "Plugin Metadata:\n"
        << "================\n"
        << "Name:         " << metadata.name << "\n"
        << "Version:      " << metadata.version << "\n"
        << "Description:  " << metadata.description << "\n"
        << "Author:       " << metadata.author << "\n"
        << "Type:         " << metadata.type << "\n";
    
    if (!metadata.dependencies.isEmpty()) {
        out << "Dependencies: " << metadata.dependencies.join(", ") << "\n";
    } else {
        out << "Dependencies: (none)\n";
    }
}

void printMetadataJson(const ModuleMetadata& metadata) {
    QJsonObject obj;
    obj["name"] = metadata.name;
    obj["version"] = metadata.version;
    obj["description"] = metadata.description;
    obj["author"] = metadata.author;
    obj["type"] = metadata.type;
    
    QJsonArray deps;
    for (const QString& dep : metadata.dependencies) {
        deps.append(dep);
    }
    obj["dependencies"] = deps;
    
    QJsonDocument doc(obj);
    out << doc.toJson(QJsonDocument::Indented);
}

void printMethodsHuman(const std::vector<MethodInfo>& methods) {
    out << "Plugin Methods:\n"
        << "===============\n\n";
    
    if (methods.empty()) {
        out << "(no methods found)\n";
        return;
    }
    
    for (const auto& method : methods) {
        out << method.returnType << " " << method.name << "(";
        
        bool first = true;
        for (const auto& param : method.parameters) {
            if (!first) out << ", ";
            out << param.type << " " << param.name;
            first = false;
        }
        
        out << ")\n";
        out << "  Signature: " << method.signature << "\n";
        out << "  Invokable: " << (method.isInvokable ? "yes" : "no") << "\n\n";
    }
}

void printMethodsJson(QObject* plugin) {
    QJsonArray methodsArray = ModuleIntrospection::getMethodsAsJson(plugin);
    QJsonDocument doc(methodsArray);
    out << doc.toJson(QJsonDocument::Indented);
}

int cmdMetadata(const QString& pluginPath, bool jsonOutput) {
    // Resolve to absolute path
    QFileInfo fileInfo(pluginPath);
    QString absolutePath = fileInfo.absoluteFilePath();
    
    // Check if file exists
    if (!fileInfo.exists()) {
        err << "Error: Plugin file not found: " << pluginPath << Qt::endl;
        return 1;
    }
    
    // Load the plugin
    QString errorString;
    ModuleHandle handle = ModuleLoader::loadFromPath(absolutePath, &errorString);
    
    if (!handle.isValid()) {
        err << "Error: Failed to load plugin: " << errorString << Qt::endl;
        return 1;
    }
    
    const ModuleMetadata& metadata = handle.metadata();
    
    if (jsonOutput) {
        printMetadataJson(metadata);
    } else {
        printMetadataHuman(metadata);
    }
    
    return 0;
}

int cmdMethods(const QString& pluginPath, bool jsonOutput) {
    // Resolve to absolute path
    QFileInfo fileInfo(pluginPath);
    QString absolutePath = fileInfo.absoluteFilePath();
    
    // Check if file exists
    if (!fileInfo.exists()) {
        err << "Error: Plugin file not found: " << pluginPath << Qt::endl;
        return 1;
    }
    
    // Load the plugin
    QString errorString;
    ModuleHandle handle = ModuleLoader::loadFromPath(absolutePath, &errorString);
    
    if (!handle.isValid()) {
        err << "Error: Failed to load plugin: " << errorString << Qt::endl;
        return 1;
    }
    
    if (!handle.instance()) {
        err << "Error: Plugin loaded but instance is null" << Qt::endl;
        return 1;
    }
    
    if (jsonOutput) {
        printMethodsJson(handle.instance());
    } else {
        auto methods = ModuleIntrospection::getMethods(handle.instance());
        printMethodsHuman(methods);
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    // Parse arguments manually
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    
    // Handle no arguments
    if (args.empty()) {
        printUsage();
        return 0;
    }
    
    // Check for global flags
    std::string firstArg = args[0];
    
    if (firstArg == "--version" || firstArg == "-v") {
        printVersion();
        return 0;
    }
    
    if (firstArg == "--help" || firstArg == "-h") {
        printUsage();
        return 0;
    }
    
    // Parse command and options
    std::string command = firstArg;
    bool jsonOutput = false;
    QString pluginPath;
    
    // Check for valid command
    if (command != "metadata" && command != "methods") {
        err << "Error: Unknown command '" << QString::fromStdString(command) << "'\n"
            << "\nRun 'lm --help' to see available commands.\n";
        return 1;
    }
    
    // Parse remaining arguments
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        
        if (arg == "--help" || arg == "-h") {
            printCommandHelp(QString::fromStdString(command));
            return 0;
        } else if (arg == "--json") {
            jsonOutput = true;
        } else if (arg[0] == '-') {
            err << "Error: Unknown option '" << QString::fromStdString(arg) << "'" << Qt::endl;
            return 1;
        } else {
            // This should be the plugin path
            if (pluginPath.isEmpty()) {
                pluginPath = QString::fromStdString(arg);
            } else {
                err << "Error: Multiple plugin paths specified" << Qt::endl;
                return 1;
            }
        }
    }
    
    // Check if plugin path was provided
    if (pluginPath.isEmpty()) {
        err << "Error: Missing plugin path" << Qt::endl;
        err << "\nUsage: lm " << QString::fromStdString(command) << " [options] <plugin-path>" << Qt::endl;
        return 1;
    }
    
    // Execute command
    if (command == "metadata") {
        return cmdMetadata(pluginPath, jsonOutput);
    } else if (command == "methods") {
        return cmdMethods(pluginPath, jsonOutput);
    }
    
    return 0;
}
