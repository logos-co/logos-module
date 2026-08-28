#include <QCoreApplication>
#include <QTextStream>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QFileInfo>
#include <iostream>
#include <optional>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>

#include "logos_module.h"
#include "module_directory.h"
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
        << "Usage: lm [command] [options] <module-path>\n"
        << "\n"
        << "<module-path> is either a plugin file, or an installed module directory\n"
        << "whose manifest.json names the plugin. A directory also reports its\n"
        << "manifest, signature and installed variant; a bare plugin file has none.\n"
        << "\n"
        << "Commands:\n"
        << "  (default)   Show metadata, methods, and events (when no command specified)\n"
        << "  metadata    Show plugin metadata (name, version, description, etc.)\n"
        << "  methods     Show plugin methods and signatures\n"
        << "  events      Show plugin events and signatures\n"
        << "  verify      Check an installed module directory against its manifest\n"
        << "\n"
        << "Options:\n"
        << "  --json      Output in JSON format\n"
        << "  --variant <name>  Variant to resolve manifest.json's main with\n"
        << "              (repeatable, directories only; defaults to the installed one)\n"
        << "  --did <did:jwk:...>  Whose signature manifest.sig must carry (verify only)\n"
        << "  --debug     Show debug output from plugin loading\n"
        << "  --help, -h  Show help information\n"
        << "  --version, -v  Show version information\n"
        << "\n"
        << "Examples:\n"
        << "  lm /path/to/plugin.so\n"
        << "  lm /path/to/plugin.so --json\n"
        << "  lm /path/to/modules/my_module\n"
        << "  lm /path/to/plugins/my_ui\n"
        << "  lm metadata /path/to/plugin.so\n"
        << "  lm methods /path/to/plugin.so\n"
        << "  lm metadata /path/to/plugin.so --json\n"
        << "  lm methods /path/to/plugin.so --json --debug\n"
        << "  lm metadata /path/to/modules/my_module --variant linux-amd64\n"
        << "  lm verify /path/to/modules/my_module\n"
        << "  lm verify /path/to/modules/my_module --did did:jwk:eyJrdHkiOi...\n";
}

void printCommandHelp(const QString& command) {
    // Every command takes the same <module-path>: a plugin file or a module
    // directory. Only the object-shaped JSON commands can carry the directory
    // report, so each help text says which one the reader is looking at.
    if (command == "metadata") {
        out << "Usage: lm metadata [options] <module-path>\n"
            << "\n"
            << "Show plugin metadata including name, version, description, author,\n"
            << "type, and dependencies. Given a module directory, the JSON also\n"
            << "carries a \"module_directory\" object describing the install.\n"
            << "\n"
            << "Options:\n"
            << "  --json   Output in JSON format\n"
            << "  --variant <name>  Variant to resolve manifest.json's main with\n"
            << "  --debug  Show debug output from plugin loading\n";
    } else if (command == "methods") {
        out << "Usage: lm methods [options] <module-path>\n"
            << "\n"
            << "Show all methods exposed by the plugin via Qt's meta-object system.\n"
            << "Displays method name, signature, return type, and parameters.\n"
            << "Given a module directory, the plugin is the main from manifest.json;\n"
            << "the JSON stays a bare array, so use `lm <dir> --json` for the install.\n"
            << "\n"
            << "Options:\n"
            << "  --json   Output in JSON format\n"
            << "  --variant <name>  Variant to resolve manifest.json's main with\n"
            << "  --debug  Show debug output from plugin loading\n";
    } else if (command == "verify") {
        out << "Usage: lm verify [options] <module-directory>\n"
            << "\n"
            << "Run every check that survives installation against the directory:\n"
            << "the manifest's own field rules, whether the installed files still\n"
            << "hash to what the manifest covers, and whether main, view and the\n"
            << "icon resolve. The rules are logos-package's, the same ones\n"
            << "`lgx verify` applies to the .lgx the directory came out of.\n"
            << "\n"
            << "Signature checking happens only when you say whose signature it\n"
            << "must be. The DID inside manifest.sig is never used as the key:\n"
            << "whoever replaces a signature replaces that DID beside it.\n"
            << "\n"
            << "Exits non-zero when a check fails, or when --did was given and\n"
            << "the signature is not that DID's.\n"
            << "\n"
            << "Options:\n"
            << "  --json   Output in JSON format\n"
            << "  --variant <name>  Variant to check, when there is no `variant` file\n"
            << "  --did <did:jwk:...>  DID whose key must have signed manifest.json\n";
    } else if (command == "events") {
        out << "Usage: lm events [options] <module-path>\n"
            << "\n"
            << "Show all events the plugin can emit (its `logos_events:` section).\n"
            << "Displays event name, signature, parameters, and description.\n"
            << "Given a module directory, the plugin is the main from manifest.json;\n"
            << "the JSON stays a bare array, so use `lm <dir> --json` for the install.\n"
            << "\n"
            << "Options:\n"
            << "  --json   Output in JSON format\n"
            << "  --variant <name>  Variant to resolve manifest.json's main with\n"
            << "  --debug  Show debug output from plugin loading\n";
    }
}

void printMetadataHuman(const ModuleMetadata& metadata) {
    out << "Plugin Metadata:\n"
        << "================\n"
        << "Name:         " << metadata.name << "\n"
        << "Display name: "
        << (metadata.displayName.isEmpty()
                ? QStringLiteral("(unset — falls back to name)")
                : metadata.displayName)
        << "\n"
        << "Version:      " << metadata.version << "\n"
        << "Description:  " << metadata.description << "\n"
        << "Author:       " << metadata.author << "\n"
        << "Type:         " << metadata.type << "\n";

    // The logos-protocol semver this module was compiled against — the one
    // number governing load/call compatibility (same MAJOR <=> compatible).
    // Modules from pre-protocol builders have no stamp.
    const QString protocolVersion = metadata.rawMetadata
        .value(QStringLiteral("logos_protocol_version")).toString();
    out << "Protocol:     "
        << (protocolVersion.isEmpty()
                ? QStringLiteral("(unstamped — pre-protocol build)")
                : protocolVersion)
        << "\n";

    if (!metadata.dependencies.empty()) {
        out << "Dependencies: " << metadata.dependencyNames().join(", ") << "\n";
    } else {
        out << "Dependencies: (none)\n";
    }
}

// dirObj is null for a plugin file, which genuinely has no manifest — the key
// is then absent rather than empty, so neither can be mistaken for the other.
void printMetadataJson(const ModuleMetadata& metadata, const QJsonObject* dirObj) {
    QJsonObject obj;
    obj["name"] = metadata.name;
    if (!metadata.displayName.isEmpty())
        obj["display_name"] = metadata.displayName;
    obj["version"] = metadata.version;
    obj["description"] = metadata.description;
    obj["author"] = metadata.author;
    obj["type"] = metadata.type;
    {
        const QString protocolVersion = metadata.rawMetadata
            .value(QStringLiteral("logos_protocol_version")).toString();
        if (!protocolVersion.isEmpty())
            obj["logos_protocol_version"] = protocolVersion;
    }

    obj["dependencies"] = QJsonArray::fromStringList(metadata.dependencyNames());
    if (dirObj)
        obj["module_directory"] = *dirObj;

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
        out << "  Invokable: " << (method.isInvokable ? "yes" : "no") << "\n";
        if (!method.description.isEmpty()) {
            const QStringList descLines = method.description.split('\n');
            if (descLines.size() <= 1) {
                out << "  Description: " << method.description << "\n";
            } else {
                out << "  Description:\n";
                for (const QString& dl : descLines) {
                    out << "    " << dl << "\n";
                }
            }
        }
        out << "\n";
    }
}

void printMethodsJson(QObject* plugin) {
    QJsonArray methodsArray = LogosModule::getMethodsAsJson(plugin);
    QJsonDocument doc(methodsArray);
    out << doc.toJson(QJsonDocument::Indented);
}

void printEventsHuman(const QJsonArray& events) {
    out << "Plugin Events:\n"
        << "==============\n\n";

    if (events.isEmpty()) {
        out << "(no events found)\n";
        return;
    }

    for (const QJsonValue& ev : events) {
        const QJsonObject obj = ev.toObject();
        // Events are void/fire-and-forget — render like a void signal.
        out << "void " << obj["name"].toString() << "(";

        const QJsonArray params = obj["parameters"].toArray();
        bool first = true;
        for (const QJsonValue& pv : params) {
            const QJsonObject po = pv.toObject();
            if (!first) out << ", ";
            out << po["type"].toString() << " " << po["name"].toString();
            first = false;
        }

        out << ")\n";
        out << "  Signature: " << obj["signature"].toString() << "\n";
        const QString desc = obj["description"].toString();
        if (!desc.isEmpty()) {
            const QStringList descLines = desc.split('\n');
            if (descLines.size() <= 1) {
                out << "  Description: " << desc << "\n";
            } else {
                out << "  Description:\n";
                for (const QString& dl : descLines) {
                    out << "    " << dl << "\n";
                }
            }
        }
        out << "\n";
    }
}

void printEventsJson(QObject* plugin) {
    QJsonArray eventsArray = LogosModule::getEventsAsJson(plugin);
    QJsonDocument doc(eventsArray);
    out << doc.toJson(QJsonDocument::Indented);
}

// =============================================================================
// Module directories
// =============================================================================

QString mainResolutionName(MainResolution state) {
    switch (state) {
    case MainResolution::Resolved:       return QStringLiteral("resolved");
    case MainResolution::NoManifest:     return QStringLiteral("no_manifest");
    case MainResolution::NotDeclared:    return QStringLiteral("not_declared");
    case MainResolution::MalformedEntry: return QStringLiteral("malformed_entry");
    case MainResolution::NoVariantMatch: return QStringLiteral("no_variant_match");
    case MainResolution::FileMissing:    return QStringLiteral("file_missing");
    }
    return QStringLiteral("unknown");
}

QString moduleKindName(ModuleKind kind) {
    switch (kind) {
    case ModuleKind::Core:     return QStringLiteral("core");
    case ModuleKind::UiPlugin: return QStringLiteral("ui_plugin");
    case ModuleKind::Unknown:  return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString assetResolutionName(AssetResolution state) {
    switch (state) {
    case AssetResolution::Resolved:      return QStringLiteral("resolved");
    case AssetResolution::NotDeclared:   return QStringLiteral("not_declared");
    case AssetResolution::FileMissing:   return QStringLiteral("file_missing");
    case AssetResolution::OutsideModule: return QStringLiteral("outside_module");
    }
    return QStringLiteral("unknown");
}

QString pluginExpectationName(PluginExpectation expectation) {
    switch (expectation) {
    case PluginExpectation::Present:     return QStringLiteral("present");
    case PluginExpectation::Missing:     return QStringLiteral("missing");
    case PluginExpectation::NotExpected: return QStringLiteral("not_expected");
    }
    return QStringLiteral("unknown");
}

QString integrityStateName(IntegrityState state) {
    switch (state) {
    case IntegrityState::Ok:         return QStringLiteral("ok");
    case IntegrityState::Mismatch:   return QStringLiteral("mismatch");
    case IntegrityState::NoHash:     return QStringLiteral("no_hash");
    case IntegrityState::Unreadable: return QStringLiteral("unreadable");
    case IntegrityState::BadInput:   return QStringLiteral("bad_input");
    }
    return QStringLiteral("unknown");
}

QString signatureCheckName(SignatureCheck check) {
    switch (check) {
    case SignatureCheck::Ok:        return QStringLiteral("ok");
    case SignatureCheck::Mismatch:  return QStringLiteral("mismatch");
    case SignatureCheck::Unusable:  return QStringLiteral("unusable");
    case SignatureCheck::BadDid:    return QStringLiteral("bad_did");
    case SignatureCheck::Unsigned:  return QStringLiteral("unsigned");
    case SignatureCheck::NoMessage: return QStringLiteral("no_message");
    }
    return QStringLiteral("unknown");
}

// One line each, so a reader never has to look a state up. Every one names the
// repair, because "invalid" told nobody anything they could act on.
QString assetDescription(const AssetFile& asset) {
    switch (asset.state) {
    case AssetResolution::Resolved:
        return asset.declaredPath;
    case AssetResolution::NotDeclared:
        return QStringLiteral("(not declared)");
    case AssetResolution::FileMissing:
        return QStringLiteral("%1  MISSING — not in this directory").arg(asset.declaredPath);
    case AssetResolution::OutsideModule:
        return QStringLiteral("%1  REFUSED — resolves outside the module directory")
            .arg(asset.declaredPath);
    }
    return asset.declaredPath;
}

QString nameAgreementName(NameAgreement agreement) {
    switch (agreement) {
    case NameAgreement::Agree:               return QStringLiteral("agree");
    case NameAgreement::Disagree:            return QStringLiteral("disagree");
    case NameAgreement::ManifestNameMissing: return QStringLiteral("manifest_name_missing");
    case NameAgreement::EmbeddedNameMissing: return QStringLiteral("embedded_name_missing");
    case NameAgreement::NoPlugin:            return QStringLiteral("no_plugin");
    }
    return QStringLiteral("unknown");
}

// The keys manifest.json's `main` offers, so a variant mismatch can show the
// reader both sides of it. Empty for the plain-string form.
QStringList declaredMainVariants(const ModuleDirectory& dir) {
    if (!dir.manifest().isPresent()) {
        return QStringList();
    }
    return dir.manifest().parsed.value(QStringLiteral("main")).toObject().keys();
}

// Names the actual fault. "not found" for every one of these is useless to
// whoever hits it: a missing manifest, a manifest with no main, a main naming a
// file that was never extracted, and a main with no entry for this variant are
// four different repairs.
void reportDirectoryProblem(const ModuleDirectory& dir) {
    const QString manifestPath = QDir(dir.path()).filePath(QStringLiteral("manifest.json"));

    if (dir.directoryState() == FileState::Unreadable) {
        err << "Error: module directory is not readable: " << dir.path() << Qt::endl;
        return;
    }

    switch (dir.manifest().state) {
    case FileState::Absent:
        err << "Error: no manifest.json in module directory: " << dir.path() << "\n"
            << "  A module directory is identified by its manifest. To inspect a bare\n"
            << "  plugin, pass the plugin file itself." << Qt::endl;
        return;
    case FileState::Unreadable:
        err << "Error: manifest.json could not be read: " << manifestPath << "\n"
            << "  " << dir.manifest().error << Qt::endl;
        return;
    case FileState::Malformed:
        err << "Error: manifest.json is not valid JSON: " << manifestPath << "\n"
            << "  " << dir.manifest().error << Qt::endl;
        return;
    case FileState::Present:
        break;
    }

    switch (dir.main().state) {
    case MainResolution::NotDeclared:
        err << "Error: manifest.json declares no \"main\": " << manifestPath << "\n";
        if (dir.kind() == ModuleKind::UiPlugin) {
            // The one type allowed no main — but only when it says what to
            // render instead. Without `view` there is nothing here to run.
            err << "  A ui_qml package with no \"main\" must declare a \"view\"." << Qt::endl;
        } else {
            err << "  Nothing in this directory is named as the module's plugin." << Qt::endl;
        }
        return;
    case MainResolution::MalformedEntry:
        err << "Error: manifest.json has an unusable \"main\": " << manifestPath << "\n"
            << "  " << dir.main().error << Qt::endl;
        return;
    case MainResolution::NoVariantMatch:
        err << "Error: manifest.json declares no main for this variant: " << manifestPath << "\n"
            << "  main has: " << declaredMainVariants(dir).join(QStringLiteral(", ")) << "\n"
            << "  tried:    "
            << (dir.candidateVariants().isEmpty()
                    ? QStringLiteral("(nothing — no `variant` file here; pass --variant <name>)")
                    : dir.candidateVariants().join(QStringLiteral(", ")))
            << Qt::endl;
        return;
    case MainResolution::FileMissing:
        err << "Error: manifest.json names a main that is not in the module directory: "
            << dir.path() << "\n"
            << "  main" << (dir.main().variant.isEmpty()
                                ? QString()
                                : QStringLiteral("[%1]").arg(dir.main().variant))
            << " = " << dir.main().declaredPath << "\n"
            << "  The package was installed for another variant, or extraction was partial."
            << Qt::endl;
        return;
    case MainResolution::NoManifest:
    case MainResolution::Resolved:
        break;
    }

    err << "Error: could not resolve a plugin in module directory: " << dir.path() << Qt::endl;
}

void printDirectoryHuman(const ModuleDirectory& dir) {
    const QDir root(dir.path());

    out << "Module Directory:\n"
        << "=================\n"
        << "Path:         " << dir.path() << "\n"
        << "Type:         "
        << (dir.declaredType().isEmpty() ? QStringLiteral("(none declared)")
                                         : dir.declaredType())
        << (dir.kind() == ModuleKind::UiPlugin ? QStringLiteral("  (UI plugin)") : QString())
        << "\n"
        << "Manifest:     manifest.json (" << dir.manifest().bytes.size() << " bytes)\n"
        << "Signature:    "
        << (dir.signature().isPresent()
                ? QStringLiteral("manifest.sig (%1 bytes), claims %2")
                      .arg(dir.signature().bytes.size())
                      .arg(dir.claimedSignerDid().isEmpty()
                               ? QStringLiteral("no DID")
                               : dir.claimedSignerDid())
                : QStringLiteral("(none — package is unsigned)"))
        << "\n"
        << "Variant:      "
        << (dir.installedVariant().isEmpty() ? QStringLiteral("(no variant file)")
                                             : dir.installedVariant())
        << "\n"
        << "Main:         ";
    if (dir.pluginExpectation() == PluginExpectation::NotExpected) {
        out << "(none — this ui_qml package is QML only)";
    } else {
        out << root.relativeFilePath(dir.main().path);
        if (!dir.main().variant.isEmpty()) {
            out << "  [main." << dir.main().variant << "]";
        }
    }
    out << "\n";

    // Printed when the manifest names one, not when the type says it might.
    // A core module declaring an icon gets the line; a ui_qml package that
    // declares neither gets neither.
    if (dir.view().state != AssetResolution::NotDeclared) {
        out << "View:         " << assetDescription(dir.view()) << "\n";
    }
    if (dir.icon().state != AssetResolution::NotDeclared) {
        out << "Icon:         " << assetDescription(dir.icon()) << "\n";
    }

    out << "Payload:      " << dir.payloadEntries().size() << " file(s)\n";
    for (const QString& entry : dir.payloadEntries()) {
        out << "  " << entry << "\n";
    }

    // The impersonation shape: whoever reaches for lm because a module is
    // misbehaving should not have to compare these two names by hand.
    if (dir.compareNames() == NameAgreement::Disagree) {
        out << "\n"
            << "WARNING: this directory misrepresents its module.\n"
            << "  manifest.json name: " << dir.manifestName() << "\n"
            << "  plugin's own name:  " << dir.embeddedMetadata()->name << "\n";
    }
}

// The additive half of the JSON. Emitted only for a directory input, and only
// under the object-shaped commands — `methods`/`events` print a bare array and
// turning that into an object is exactly the break other parsers would feel.
QJsonObject assetJson(const AssetFile& asset) {
    QJsonObject obj;
    obj["state"] = assetResolutionName(asset.state);
    if (!asset.declaredPath.isEmpty())
        obj["declared"] = asset.declaredPath;
    if (!asset.path.isEmpty())
        obj["path"] = asset.path;
    return obj;
}

QJsonObject directoryJson(const ModuleDirectory& dir) {
    QJsonObject obj;
    obj["path"] = dir.path();
    obj["type"] = dir.declaredType();
    obj["kind"] = moduleKindName(dir.kind());

    QJsonObject manifest;
    manifest["size_bytes"] = static_cast<int>(dir.manifest().bytes.size());
    // The parsed view, for reading. The signed message is the file's exact
    // bytes on disk, which no JSON writer round-trips — verify against those.
    manifest["content"] = dir.manifest().parsed;
    obj["manifest"] = manifest;

    QJsonObject signature;
    signature["present"] = dir.signature().isPresent();
    signature["size_bytes"] = static_cast<int>(dir.signature().bytes.size());
    // Self-asserted, and named so. Nothing has been checked against it.
    signature["claimed_did"] = dir.claimedSignerDid();
    obj["signature"] = signature;

    if (!dir.installedVariant().isEmpty())
        obj["installed_variant"] = dir.installedVariant();
    obj["candidate_variants"] = QJsonArray::fromStringList(dir.candidateVariants());

    QJsonObject main;
    // "resolved", or "not_declared" for a QML-only ui_qml package — lm reports
    // the states that ARE faults on stderr and exits before it gets here.
    main["state"] = mainResolutionName(dir.main().state);
    main["path"] = dir.main().path;
    main["declared"] = dir.main().declaredPath;
    if (!dir.main().variant.isEmpty())
        main["variant"] = dir.main().variant;
    obj["main"] = main;

    // The verdict on main(), which needs the type to reach. A parser reading
    // only main.state cannot tell a broken module from a QML-only plugin.
    obj["plugin"] = pluginExpectationName(dir.pluginExpectation());
    obj["view"] = assetJson(dir.view());
    obj["icon"] = assetJson(dir.icon());

    obj["payload"] = QJsonArray::fromStringList(dir.payloadEntries());
    obj["name_agreement"] = nameAgreementName(dir.compareNames());
    return obj;
}

// What the path on the command line turned out to be. Resolved once, up front,
// because every command needs the same answer and a directory report must be
// printed once rather than by each command it passes through.
struct ResolvedInput {
    QString pluginPath;   ///< what to load; empty when the package has no plugin
    std::optional<ModuleDirectory> directory;  ///< only when a directory was given
};

std::optional<ResolvedInput> resolveInput(const QString& path, const QStringList& variants) {
    const QFileInfo info(path);

    if (!info.exists()) {
        err << "Error: Plugin file not found: " << path << Qt::endl;
        return std::nullopt;
    }

    if (!info.isDir()) {
        if (!variants.isEmpty()) {
            err << "Error: --variant selects a main from a module directory's manifest.json; "
                << path << " is a plugin file" << Qt::endl;
            return std::nullopt;
        }
        // Verbatim, not absolutised: the commands absolutise for themselves and
        // echo this one back in diagnostics, where it must stay what was typed.
        return ResolvedInput{path, std::nullopt};
    }

    ModuleDirectory dir = ModuleDirectory::open(path, variants);
    if (dir.pluginExpectation() == PluginExpectation::Missing) {
        reportDirectoryProblem(dir);
        return std::nullopt;
    }
    // NotExpected leaves pluginPath empty: a QML-only ui_qml package has no
    // plugin, and that is a complete package, not a fault to report.
    return ResolvedInput{dir.main().path, std::move(dir)};
}

// Every plugin command needs a plugin. A QML-only ui_qml package has none,
// which is an absence to state rather than a failure to report — so each of
// them says so and succeeds.
int cmdWithoutPlugin(const QString& command, bool jsonOutput, const QJsonObject* dirObj) {
    if (!jsonOutput) {
        out << "(no plugin — this ui_qml package is QML only)\n";
        return 0;
    }
    if (command == "methods" || command == "events") {
        // Still a bare array: a parser reading these should not have to
        // special-case the one package type with nothing to list.
        out << QJsonDocument(QJsonArray()).toJson(QJsonDocument::Indented);
        return 0;
    }
    QJsonObject obj;
    if (dirObj)
        obj["module_directory"] = *dirObj;
    out << QJsonDocument(obj).toJson(QJsonDocument::Indented);
    return 0;
}

// =============================================================================
// Verification
// =============================================================================

QString integrityLine(const InstalledChecks& checks) {
    switch (checks.integrity) {
    case IntegrityState::Ok:
        return QStringLiteral("ok — the installed files hash to hashes[\"variants/%1\"]")
            .arg(checks.variant);
    case IntegrityState::Mismatch:
        return QStringLiteral("MISMATCH — %1").arg(checks.integrityDetail);
    case IntegrityState::NoHash:
        return QStringLiteral("not proved — %1").arg(checks.integrityDetail);
    case IntegrityState::Unreadable:
        return QStringLiteral("could not run — %1").arg(checks.integrityDetail);
    case IntegrityState::BadInput:
        return QStringLiteral("not checked — %1").arg(checks.integrityDetail);
    }
    return checks.integrityDetail;
}

QString signatureLine(const ModuleDirectory& dir, const QString& expectedDid,
                      const std::optional<SignatureCheck>& check) {
    const QString claimed = dir.claimedSignerDid();
    if (!check) {
        if (!dir.signature().isPresent()) {
            return QStringLiteral("not checked — this package is unsigned");
        }
        // The claimed DID is shown so a human can decide what to pin. It is
        // never what gets checked: see ModuleDirectory::checkSignature().
        return QStringLiteral("not checked — pass --did to check it "
                              "(manifest.sig claims %1)")
            .arg(claimed.isEmpty() ? QStringLiteral("no DID") : claimed);
    }
    switch (*check) {
    case SignatureCheck::Ok:
        return QStringLiteral("ok — %1 signed this manifest").arg(expectedDid);
    case SignatureCheck::Mismatch:
        return QStringLiteral("MISMATCH — %1 did not sign these bytes "
                              "(manifest.sig claims %2)")
            .arg(expectedDid, claimed.isEmpty() ? QStringLiteral("no DID") : claimed);
    case SignatureCheck::Unusable:
        return QStringLiteral("unusable — manifest.sig is not a usable Ed25519 "
                              "signature document");
    case SignatureCheck::BadDid:
        return QStringLiteral("bad --did — '%1' is not a did:jwk carrying an "
                              "Ed25519 key").arg(expectedDid);
    case SignatureCheck::Unsigned:
        return QStringLiteral("unsigned — there is no manifest.sig here to check");
    case SignatureCheck::NoMessage:
        return QStringLiteral("no message — there is no readable manifest.json "
                              "to have been signed");
    }
    return QString();
}

// Reports; the exit status is lm's own call, made by the caller below.
void printVerifyHuman(const ModuleDirectory& dir, const InstalledChecks& checks,
                      const QString& expectedDid,
                      const std::optional<SignatureCheck>& signature) {
    out << "Verification:\n"
        << "=============\n"
        << "Path:       " << dir.path() << "\n"
        << "Variant:    "
        << (checks.variant.isEmpty() ? QStringLiteral("(none)") : checks.variant) << "\n"
        << "Integrity:  " << integrityLine(checks) << "\n"
        << "Signature:  " << signatureLine(dir, expectedDid, signature) << "\n";

    if (checks.errors.isEmpty()) {
        out << "Findings:   none\n";
    } else {
        out << "Findings:   " << checks.errors.size() << "\n";
        for (const QString& e : checks.errors) {
            out << "  - " << e << "\n";
        }
    }
    for (const QString& w : checks.warnings) {
        out << "  ! " << w << "\n";
    }
}

void printVerifyJson(const ModuleDirectory& dir, const InstalledChecks& checks,
                     const QString& expectedDid,
                     const std::optional<SignatureCheck>& signature) {
    QJsonObject obj;
    obj["path"] = dir.path();
    obj["ran"] = checks.ran;
    obj["valid"] = checks.valid;
    obj["variant"] = checks.variant;
    obj["integrity"] = integrityStateName(checks.integrity);
    if (!checks.integrityDetail.isEmpty())
        obj["integrity_detail"] = checks.integrityDetail;
    obj["errors"] = QJsonArray::fromStringList(checks.errors);
    obj["warnings"] = QJsonArray::fromStringList(checks.warnings);

    QJsonObject sig;
    sig["state"] = signature ? signatureCheckName(*signature)
                             : QStringLiteral("not_checked");
    sig["present"] = dir.signature().isPresent();
    // Labelled `claimed_did`, never `signer`: nothing has been proved about it.
    sig["claimed_did"] = dir.claimedSignerDid();
    if (!expectedDid.isEmpty())
        sig["expected_did"] = expectedDid;
    obj["signature"] = sig;

    out << QJsonDocument(obj).toJson(QJsonDocument::Indented);
}

int cmdVerify(const ModuleDirectory& dir, const QString& expectedDid, bool jsonOutput) {
    const InstalledChecks checks = dir.checkInstalled();

    // No DID means no signature question was asked, so none is answered. There
    // is deliberately no way to ask "is the signature good?" without naming
    // whose it must be.
    std::optional<SignatureCheck> signature;
    if (!expectedDid.isEmpty()) {
        signature = dir.checkSignature(expectedDid);
    }

    if (jsonOutput) {
        printVerifyJson(dir, checks, expectedDid, signature);
    } else {
        printVerifyHuman(dir, checks, expectedDid, signature);
    }

    const bool signatureFailed = signature && *signature != SignatureCheck::Ok;
    const bool failed = !checks.ran || !checks.valid || signatureFailed;
    if (!jsonOutput) {
        out << "\nRESULT: " << (failed ? "FAIL" : "pass") << "\n";
    }
    return failed ? 1 : 0;
}

// The commands below are only ever reached through resolveInput(), which has
// already proved the path exists — it is the plugin file the user named, or the
// main it read out of a manifest — so none of them re-checks for one.
int cmdMetadata(const QString& pluginPath, bool jsonOutput, const QJsonObject* dirObj) {
    QString absolutePath = QFileInfo(pluginPath).absoluteFilePath();

    auto metadata = LogosModule::extractMetadata(absolutePath);
    if (!metadata) {
        err << "Error: Failed to extract metadata from: " << pluginPath << Qt::endl;
        return 1;
    }

    if (jsonOutput) {
        printMetadataJson(*metadata, dirObj);
    } else {
        printMetadataHuman(*metadata);
    }

    return 0;
}

int cmdMethods(const QString& pluginPath, bool jsonOutput, bool debugOutput) {
    QString absolutePath = QFileInfo(pluginPath).absoluteFilePath();

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

int cmdEvents(const QString& pluginPath, bool jsonOutput, bool debugOutput) {
    QString absolutePath = QFileInfo(pluginPath).absoluteFilePath();

    QString errorString;
    LogosModule plugin;

    if (!debugOutput) {
        int stdout_copy = dup(STDOUT_FILENO);
        int stderr_copy = dup(STDERR_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        plugin = LogosModule::loadFromPath(absolutePath, &errorString);
        if (stdout_copy != -1) { dup2(stdout_copy, STDOUT_FILENO); close(stdout_copy); }
        if (stderr_copy != -1) { dup2(stderr_copy, STDERR_FILENO); close(stderr_copy); }
    } else {
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
        printEventsJson(plugin.instance());
    } else {
        printEventsHuman(plugin.getEventsAsJson());
    }

    return 0;
}

int cmdInfo(const QString& pluginPath, bool jsonOutput, bool debugOutput,
            const QJsonObject* dirObj) {
    QString absolutePath = QFileInfo(pluginPath).absoluteFilePath();

    if (jsonOutput) {
        // For JSON output, combine metadata and methods into a single object
        auto metadata = LogosModule::extractMetadata(absolutePath);
        if (!metadata) {
            err << "Error: Failed to extract metadata from: " << pluginPath << Qt::endl;
            return 1;
        }
        
        // Load plugin for methods
        QString errorString;
        LogosModule plugin;
        
        if (!debugOutput) {
            // Suppress output during loading
            int stdout_copy = dup(STDOUT_FILENO);
            int stderr_copy = dup(STDERR_FILENO);
            
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            
            plugin = LogosModule::loadFromPath(absolutePath, &errorString);
            
            if (stdout_copy != -1) {
                dup2(stdout_copy, STDOUT_FILENO);
                close(stdout_copy);
            }
            if (stderr_copy != -1) {
                dup2(stderr_copy, STDERR_FILENO);
                close(stderr_copy);
            }
        } else {
            plugin = LogosModule::loadFromPath(absolutePath, &errorString);
        }
        
        if (!plugin.isValid()) {
            err << "Error: Failed to load plugin: " << errorString << Qt::endl;
            return 1;
        }
        
        // Build combined JSON object
        QJsonObject combined;
        
        QJsonObject metadataObj;
        metadataObj["name"] = metadata->name;
        if (!metadata->displayName.isEmpty())
            metadataObj["display_name"] = metadata->displayName;
        metadataObj["version"] = metadata->version;
        metadataObj["description"] = metadata->description;
        metadataObj["author"] = metadata->author;
        metadataObj["type"] = metadata->type;
        
        metadataObj["dependencies"] = QJsonArray::fromStringList(metadata->dependencyNames());
        
        combined["metadata"] = metadataObj;
        combined["methods"] = LogosModule::getMethodsAsJson(plugin.instance());
        combined["events"] = LogosModule::getEventsAsJson(plugin.instance());
        if (dirObj)
            combined["module_directory"] = *dirObj;

        QJsonDocument doc(combined);
        out << doc.toJson(QJsonDocument::Indented);
    } else {
        // The directory report, if any, was printed once by main() ahead of
        // this, so the three sections below stay exactly as a plugin file's.
        int result = cmdMetadata(pluginPath, false, nullptr);
        if (result != 0) {
            return result;
        }

        out << "\n";
        result = cmdMethods(pluginPath, false, debugOutput);
        if (result != 0) {
            return result;
        }

        out << "\n";
        return cmdEvents(pluginPath, false, debugOutput);
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
    
    std::string command;
    bool defaultMode = false;
    bool jsonOutput = false;
    bool debugOutput = false;
    QString pluginPath;
    QStringList variants;
    QString expectedDid;

    // Check if first arg is a command or a plugin path
    if (firstArg == "metadata" || firstArg == "methods" || firstArg == "events" ||
        firstArg == "verify") {
        command = firstArg;
    } else if (firstArg[0] != '-') {
        // First arg is not a command and not an option, treat as plugin path
        defaultMode = true;
        pluginPath = QString::fromStdString(firstArg);
    } else {
        err << "Error: Unknown option '" << QString::fromStdString(firstArg) << "'" << Qt::endl;
        return 1;
    }
    
    // Parse remaining arguments
    size_t startIdx = defaultMode ? 1 : 1;  // Start after command or plugin path
    for (size_t i = startIdx; i < args.size(); ++i) {
        std::string arg = args[i];
        
        if (arg == "--help" || arg == "-h") {
            if (defaultMode) {
                printUsage();
            } else {
                printCommandHelp(QString::fromStdString(command));
            }
            return 0;
        } else if (arg == "--json") {
            jsonOutput = true;
        } else if (arg == "--variant") {
            if (i + 1 >= args.size()) {
                err << "Error: --variant needs a variant name" << Qt::endl;
                return 1;
            }
            variants.append(QString::fromStdString(args[++i]));
        } else if (arg == "--did") {
            if (i + 1 >= args.size()) {
                err << "Error: --did needs a did:jwk value" << Qt::endl;
                return 1;
            }
            expectedDid = QString::fromStdString(args[++i]);
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
        if (defaultMode) {
            err << "\nUsage: lm [options] <module-path>" << Qt::endl;
        } else {
            err << "\nUsage: lm " << QString::fromStdString(command) << " [options] <module-path>" << Qt::endl;
        }
        return 1;
    }

    // Set global debug flag
    g_debugMode = debugOutput;

    if (!expectedDid.isEmpty() && command != "verify") {
        err << "Error: --did names whose signature to check, which only "
            << "`lm verify` does" << Qt::endl;
        return 1;
    }

    // verify does not go through resolveInput(): it is the command for a
    // directory that may be broken, so bailing out on a broken one would
    // suppress exactly the report it exists to print.
    if (command == "verify") {
        const QFileInfo info(pluginPath);
        if (!info.exists()) {
            err << "Error: module directory not found: " << pluginPath << Qt::endl;
            return 1;
        }
        if (!info.isDir()) {
            err << "Error: verify takes an installed module directory; "
                << pluginPath << " is a file\n"
                << "  The checks are over manifest.json and the files it covers, "
                << "and a bare plugin has neither." << Qt::endl;
            return 1;
        }
        return cmdVerify(ModuleDirectory::open(pluginPath, variants), expectedDid,
                         jsonOutput);
    }

    std::optional<ResolvedInput> input = resolveInput(pluginPath, variants);
    if (!input) {
        return 1;
    }

    // The directory report goes first and only once, whatever command follows.
    // JSON carries it inside the document instead, where the document is an
    // object; `methods`/`events` print a bare array and get none.
    std::optional<QJsonObject> dirObj;
    if (input->directory) {
        if (jsonOutput) {
            dirObj = directoryJson(*input->directory);
        } else {
            printDirectoryHuman(*input->directory);
            out << "\n";
        }
    }
    const QJsonObject* dirObjPtr = dirObj ? &*dirObj : nullptr;

    if (input->pluginPath.isEmpty()) {
        return cmdWithoutPlugin(QString::fromStdString(command), jsonOutput, dirObjPtr);
    }

    if (defaultMode) {
        return cmdInfo(input->pluginPath, jsonOutput, debugOutput, dirObjPtr);
    } else if (command == "metadata") {
        return cmdMetadata(input->pluginPath, jsonOutput, dirObjPtr);
    } else if (command == "methods") {
        return cmdMethods(input->pluginPath, jsonOutput, debugOutput);
    } else if (command == "events") {
        return cmdEvents(input->pluginPath, jsonOutput, debugOutput);
    }

    return 0;
}
