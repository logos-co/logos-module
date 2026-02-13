#include <QCoreApplication>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>

#include "logos_module.h"
#include "module_metadata.h"

using namespace ModuleLib;

QTextStream out(stdout);
QTextStream err(stderr);

const char* VERSION = "0.1.0";

// Global flag for debug mode
static bool g_debugMode = false;

// Custom Qt message handler to suppress debug/info messages unless in debug mode
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (!g_debugMode && (type == QtDebugMsg || type == QtInfoMsg)) {
        // Suppress debug and info messages when not in debug mode
        return;
    }
    
    // For warnings and errors, or when in debug mode, use default handling
    QByteArray localMsg = msg.toLocal8Bit();
    const char* file = context.file ? context.file : "";
    const char* function = context.function ? context.function : "";
    
    switch (type) {
    case QtDebugMsg:
        fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        abort();
    }
}

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
        << "  --debug     Show debug output from plugin loading\n"
        << "  --help, -h  Show help information\n"
        << "  --version, -v  Show version information\n"
        << "\n"
        << "Examples:\n"
        << "  lm metadata /path/to/plugin.so\n"
        << "  lm methods /path/to/plugin.so\n"
        << "  lm metadata /path/to/plugin.so --json\n"
        << "  lm methods /path/to/plugin.so --json --debug\n";
}

void printCommandHelp(const QString& command) {
    if (command == "metadata") {
        out << "Usage: lm metadata [options] <plugin-path>\n"
            << "\n"
            << "Show plugin metadata including name, version, description, author,\n"
            << "type, and dependencies.\n"
            << "\n"
            << "Options:\n"
            << "  --json   Output in JSON format\n"
            << "  --debug  Show debug output from plugin loading\n";
    } else if (command == "methods") {
        out << "Usage: lm methods [options] <plugin-path>\n"
            << "\n"
            << "Show all methods exposed by the plugin via Qt's meta-object system.\n"
            << "Displays method name, signature, return type, and parameters.\n"
            << "\n"
            << "Options:\n"
            << "  --json   Output in JSON format\n"
            << "  --debug  Show debug output from plugin loading\n";
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
    QJsonArray methodsArray = LogosModule::getMethodsAsJson(plugin);
    QJsonDocument doc(methodsArray);
    out << doc.toJson(QJsonDocument::Indented);
}

int cmdMetadata(const QString& pluginPath, bool jsonOutput) {
    QFileInfo fileInfo(pluginPath);
    QString absolutePath = fileInfo.absoluteFilePath();
    
    if (!fileInfo.exists()) {
        err << "Error: Plugin file not found: " << pluginPath << Qt::endl;
        return 1;
    }
    
    auto metadata = LogosModule::extractMetadata(absolutePath);
    if (!metadata) {
        err << "Error: Failed to extract metadata from: " << pluginPath << Qt::endl;
        return 1;
    }
    
    if (jsonOutput) {
        printMetadataJson(*metadata);
    } else {
        printMetadataHuman(*metadata);
    }
    
    return 0;
}

int cmdMethods(const QString& pluginPath, bool jsonOutput, bool debugOutput) {
    QFileInfo fileInfo(pluginPath);
    QString absolutePath = fileInfo.absoluteFilePath();
    
    if (!fileInfo.exists()) {
        err << "Error: Plugin file not found: " << pluginPath << Qt::endl;
        return 1;
    }
    
    QString errorString;
    LogosModule plugin;
    
    if (!debugOutput) {
        // Redirect stdout and stderr to /dev/null during plugin loading
        int stdout_copy = dup(STDOUT_FILENO);
        int stderr_copy = dup(STDERR_FILENO);
        
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        
        // Load the plugin (this may trigger constructor output)
        plugin = LogosModule::loadFromPath(absolutePath, &errorString);
        
        // Restore stdout and stderr
        if (stdout_copy != -1) {
            dup2(stdout_copy, STDOUT_FILENO);
            close(stdout_copy);
        }
        if (stderr_copy != -1) {
            dup2(stderr_copy, STDERR_FILENO);
            close(stderr_copy);
        }
    } else {
        // Debug mode: load normally without suppression
        plugin = LogosModule::loadFromPath(absolutePath, &errorString);
    }
    
    if (!plugin.isValid()) {
        err << "Error: Failed to load plugin: " << errorString << Qt::endl;
        return 1;
    }
    
    if (!plugin.instance()) {
        err << "Error: Plugin loaded but instance is null" << Qt::endl;
        return 1;
    }
    
    if (jsonOutput) {
        printMethodsJson(plugin.instance());
    } else {
        auto methods = plugin.getMethods();
        printMethodsHuman(methods);
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    // Install custom message handler to suppress debug output by default
    qInstallMessageHandler(customMessageHandler);
    
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    
    if (args.empty()) {
        printUsage();
        return 0;
    }
    
    std::string firstArg = args[0];
    
    if (firstArg == "--version" || firstArg == "-v") {
        printVersion();
        return 0;
    }
    
    if (firstArg == "--help" || firstArg == "-h") {
        printUsage();
        return 0;
    }
    
    std::string command = firstArg;
    bool jsonOutput = false;
    bool debugOutput = false;
    QString pluginPath;
    
    if (command != "metadata" && command != "methods") {
        err << "Error: Unknown command '" << QString::fromStdString(command) << "'\n"
            << "\nRun 'lm --help' to see available commands.\n";
        return 1;
    }
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        
        if (arg == "--help" || arg == "-h") {
            printCommandHelp(QString::fromStdString(command));
            return 0;
        } else if (arg == "--json") {
            jsonOutput = true;
        } else if (arg == "--debug") {
            debugOutput = true;
        } else if (arg[0] == '-') {
            err << "Error: Unknown option '" << QString::fromStdString(arg) << "'" << Qt::endl;
            return 1;
        } else {
            if (pluginPath.isEmpty()) {
                pluginPath = QString::fromStdString(arg);
            } else {
                err << "Error: Multiple plugin paths specified" << Qt::endl;
                return 1;
            }
        }
    }
    
    if (pluginPath.isEmpty()) {
        err << "Error: Missing plugin path" << Qt::endl;
        err << "\nUsage: lm " << QString::fromStdString(command) << " [options] <plugin-path>" << Qt::endl;
        return 1;
    }
    
    // Set global debug flag
    g_debugMode = debugOutput;
    
    if (command == "metadata") {
        return cmdMetadata(pluginPath, jsonOutput);
    } else if (command == "methods") {
        return cmdMethods(pluginPath, jsonOutput, debugOutput);
    }
    
    return 0;
}
