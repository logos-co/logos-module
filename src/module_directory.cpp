#include "module_directory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace ModuleLib {

namespace {

const char* const kManifestFileName = "manifest.json";
const char* const kSignatureFileName = "manifest.sig";
const char* const kVariantFileName = "variant";

// Reads a file as bytes, never as text: manifest.json is a signed message and
// any newline translation would silently break every signature over it.
ModuleFile readFile(const QString& filePath)
{
    ModuleFile result;

    const QFileInfo info(filePath);
    if (!info.exists()) {
        result.state = FileState::Absent;
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.state = FileState::Unreadable;
        result.error = file.errorString();
        return result;
    }

    result.bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        result.state = FileState::Unreadable;
        result.error = file.errorString();
        result.bytes.clear();
        return result;
    }

    result.state = FileState::Present;
    return result;
}

ModuleFile unreadable(const QString& why)
{
    ModuleFile result;
    result.state = FileState::Unreadable;
    result.error = why;
    return result;
}

ManifestFile readManifest(const QString& filePath)
{
    const ModuleFile raw = readFile(filePath);

    ManifestFile result;
    result.state = raw.state;
    result.bytes = raw.bytes;
    result.error = raw.error;
    if (result.state != FileState::Present) {
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(result.bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.state = FileState::Malformed;
        result.error = QStringLiteral("%1 at offset %2")
                           .arg(parseError.errorString())
                           .arg(parseError.offset);
        return result;
    }
    if (!doc.isObject()) {
        result.state = FileState::Malformed;
        result.error = QStringLiteral("manifest is not a JSON object");
        return result;
    }

    result.parsed = doc.object();
    return result;
}

// Trailing whitespace and CR are stripped so the comparison against a variant
// name is exact whichever platform wrote the file.
QString trimVariant(const QByteArray& bytes)
{
    QString variant = QString::fromUtf8(bytes);
    const int newline = variant.indexOf(QLatin1Char('\n'));
    if (newline >= 0) {
        variant.truncate(newline);
    }
    while (!variant.isEmpty() && variant.back().isSpace()) {
        variant.chop(1);
    }
    return variant;
}

// The directory IS the module, so a declared path leaving it does not name
// anything of this module's, whatever it names. Empty when it escapes.
QString containedPath(const QDir& dir, const QString& declaredPath)
{
    const QString root = QDir::cleanPath(dir.absolutePath());
    const QString candidate = QDir::cleanPath(dir.absoluteFilePath(declaredPath));
    if (candidate != root && !candidate.startsWith(root + QLatin1Char('/'))) {
        return QString();
    }
    return candidate;
}

MainFile locateMain(const QDir& dir, const QString& declaredPath, const QString& variant)
{
    MainFile result;
    result.declaredPath = declaredPath;
    result.variant = variant;

    const QString candidate = containedPath(dir, declaredPath);
    if (candidate.isEmpty()) {
        result.state = MainResolution::MalformedEntry;
        result.error = QStringLiteral("main '%1' resolves outside the module directory")
                           .arg(declaredPath);
        return result;
    }

    // isFile(), not exists(): a directory named as `main` is not a plugin, and
    // exists() would hand the caller a path it cannot load. Symlinks are
    // followed, so a symlinked plugin still resolves.
    const QFileInfo candidateInfo(candidate);
    if (!candidateInfo.exists()) {
        result.state = MainResolution::FileMissing;
        result.error = QStringLiteral("main '%1' is not present in the module directory")
                           .arg(declaredPath);
        return result;
    }
    if (!candidateInfo.isFile()) {
        result.state = MainResolution::MalformedEntry;
        result.error = QStringLiteral("main '%1' is not a file")
                           .arg(declaredPath);
        return result;
    }

    result.state = MainResolution::Resolved;
    result.path = candidate;
    return result;
}

// Mirrors the package manager's resolveMainFilePath: the first candidate
// variant that is a key wins outright, and a named-but-missing file does NOT
// fall through to a later variant. Same resolution, better reporting — where
// that function returns one empty string this names the reason, and where it
// would throw on a non-string value this reports MalformedEntry.
MainFile resolveMain(const QJsonObject& manifest, const QDir& dir, const QStringList& variants)
{
    MainFile result;

    if (!manifest.contains(QStringLiteral("main"))) {
        result.state = MainResolution::NotDeclared;
        return result;
    }
    const QJsonValue mainValue = manifest.value(QStringLiteral("main"));

    if (mainValue.isObject()) {
        const QJsonObject mainObject = mainValue.toObject();
        QString unusable;
        for (const QString& variant : variants) {
            if (!mainObject.contains(variant)) {
                continue;
            }
            const QJsonValue declared = mainObject.value(variant);
            if (!declared.isString() || declared.toString().isEmpty()) {
                if (unusable.isEmpty()) {
                    unusable = variant;
                }
                continue;
            }
            return locateMain(dir, declared.toString(), variant);
        }
        if (!unusable.isEmpty()) {
            result.state = MainResolution::MalformedEntry;
            result.variant = unusable;
            result.error = QStringLiteral("main['%1'] is not a non-empty string").arg(unusable);
            return result;
        }
        result.state = MainResolution::NoVariantMatch;
        result.error = QStringLiteral("no candidate variant [%1] is a key of main")
                           .arg(variants.join(QStringLiteral(", ")));
        return result;
    }

    if (mainValue.isString()) {
        const QString declared = mainValue.toString();
        if (declared.isEmpty()) {
            result.state = MainResolution::MalformedEntry;
            result.error = QStringLiteral("main is an empty string");
            return result;
        }
        return locateMain(dir, declared, QString());
    }

    result.state = MainResolution::MalformedEntry;
    result.error = QStringLiteral("main is neither a variant map nor a string");
    return result;
}

// `view` and `icon`: one relative path each, resolved the same way. Reported
// for every directory, so a caller never tests the type to ask.
AssetFile resolveAsset(const QJsonObject& manifest, const QDir& dir, const QString& field)
{
    AssetFile result;

    const QString declared = manifest.value(field).toString();
    if (declared.isEmpty()) {
        result.state = AssetResolution::NotDeclared;
        return result;
    }
    result.declaredPath = declared;

    const QString candidate = containedPath(dir, declared);
    if (candidate.isEmpty()) {
        result.state = AssetResolution::OutsideModule;
        result.error = QStringLiteral("%1 '%2' resolves outside the module directory")
                           .arg(field, declared);
        return result;
    }
    if (!QFileInfo(candidate).isFile()) {
        result.state = AssetResolution::FileMissing;
        result.error = QStringLiteral("%1 '%2' is not present in the module directory")
                           .arg(field, declared);
        return result;
    }

    result.state = AssetResolution::Resolved;
    result.path = candidate;
    return result;
}

// Mirrors logos-package's viewOnlyUiQml: type is ui_qml, `view` is set, and
// `main` names no variant at all. An EMPTY main object counts as none there,
// so it counts as none here — a partially populated main gets the strict check.
bool declaresNoMain(const QJsonObject& manifest)
{
    const QJsonValue main = manifest.value(QStringLiteral("main"));
    return main.isUndefined() || (main.isObject() && main.toObject().isEmpty());
}

QStringList readPayloadEntries(const QDir& dir)
{
    QStringList entries = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                        QDir::Name);
    entries.removeAll(QString::fromLatin1(kManifestFileName));
    entries.removeAll(QString::fromLatin1(kSignatureFileName));
    entries.removeAll(QString::fromLatin1(kVariantFileName));
    return entries;
}

}  // namespace

ModuleDirectory ModuleDirectory::open(const QString& directoryPath,
                                      const QStringList& candidateVariants)
{
    ModuleDirectory result;
    result.m_candidateVariants = candidateVariants;

    const QFileInfo info(directoryPath);
    result.m_path = info.absoluteFilePath();

    if (!info.exists()) {
        result.m_directoryState = FileState::Absent;
        return result;
    }
    if (!info.isDir()) {
        result.m_directoryState = FileState::Malformed;
        return result;
    }
    if (!info.isReadable()) {
        // Report every member as unknown rather than Absent: an unlistable
        // directory says nothing about what is or is not inside it.
        const QString why = QStringLiteral("module directory is not readable");
        result.m_directoryState = FileState::Unreadable;
        result.m_signature = unreadable(why);
        result.m_variantFile = unreadable(why);
        result.m_manifest.state = FileState::Unreadable;
        result.m_manifest.error = why;
        result.m_main.state = MainResolution::NoManifest;
        result.m_main.error = why;
        return result;
    }

    const QDir dir(result.m_path);
    result.m_directoryState = FileState::Present;
    result.m_manifest = readManifest(dir.filePath(QString::fromLatin1(kManifestFileName)));
    result.m_signature = readFile(dir.filePath(QString::fromLatin1(kSignatureFileName)));
    result.m_variantFile = readFile(dir.filePath(QString::fromLatin1(kVariantFileName)));
    result.m_payloadEntries = readPayloadEntries(dir);

    if (result.m_variantFile.isPresent()) {
        result.m_installedVariant = trimVariant(result.m_variantFile.bytes);
        if (result.m_installedVariant.isEmpty()) {
            result.m_variantFile.state = FileState::Malformed;
            result.m_variantFile.error = QStringLiteral("variant file is blank");
        }
    }

    // The installed variant is the fallback candidate because it is read off
    // this very directory — it resolves the main with no spelling table.
    if (result.m_candidateVariants.isEmpty() && !result.m_installedVariant.isEmpty()) {
        result.m_candidateVariants.append(result.m_installedVariant);
    }

    if (result.m_manifest.isPresent()) {
        result.m_main = resolveMain(result.m_manifest.parsed, dir, result.m_candidateVariants);
        result.m_view = resolveAsset(result.m_manifest.parsed, dir, QStringLiteral("view"));
        result.m_icon = resolveAsset(result.m_manifest.parsed, dir, QStringLiteral("icon"));
    } else {
        result.m_main.state = MainResolution::NoManifest;
        result.m_main.error = result.m_manifest.error;
    }
    result.m_pluginExpectation = result.computePluginExpectation();

    return result;
}

ModuleDirectory ModuleDirectory::open(const std::string& directoryPath,
                                      const std::vector<std::string>& candidateVariants)
{
    QStringList variants;
    variants.reserve(static_cast<int>(candidateVariants.size()));
    for (const std::string& variant : candidateVariants) {
        variants.append(QString::fromStdString(variant));
    }
    return open(QString::fromStdString(directoryPath), variants);
}

QString ModuleDirectory::manifestName() const
{
    if (!m_manifest.isPresent()) {
        return QString();
    }
    return m_manifest.parsed.value(QStringLiteral("name")).toString();
}

const std::optional<ModuleMetadata>& ModuleDirectory::embeddedMetadata() const
{
    if (!m_embeddedProbed) {
        m_embeddedProbed = true;
        if (m_main.isResolved()) {
            m_embedded = ModuleMetadata::fromPath(m_main.path);
        }
    }
    return m_embedded;
}

QString ModuleDirectory::declaredType() const
{
    if (!m_manifest.isPresent()) {
        return QString();
    }
    return m_manifest.parsed.value(QStringLiteral("type")).toString();
}

ModuleKind ModuleDirectory::kind() const
{
    const QString type = declaredType();
    if (type == QLatin1String("ui_qml")) {
        return ModuleKind::UiPlugin;
    }
    // Every other declared type is its plugin. Unknown is reserved for a
    // manifest that names none, where nothing can be concluded.
    return type.isEmpty() ? ModuleKind::Unknown : ModuleKind::Core;
}

PluginExpectation ModuleDirectory::computePluginExpectation() const
{
    if (m_main.isResolved()) {
        return PluginExpectation::Present;
    }
    if (kind() == ModuleKind::UiPlugin &&
        m_view.state != AssetResolution::NotDeclared &&
        declaresNoMain(m_manifest.parsed)) {
        return PluginExpectation::NotExpected;
    }
    return PluginExpectation::Missing;
}

QString ModuleDirectory::checkedVariant() const
{
    if (!m_installedVariant.isEmpty()) {
        return m_installedVariant;
    }
    // No `variant` sidecar: the key that resolved `main` is the only other
    // statement this directory makes about which variant is on disk.
    return m_main.variant;
}

NameAgreement ModuleDirectory::compareNames() const
{
    const QString declared = manifestName();
    if (declared.isEmpty()) {
        return NameAgreement::ManifestNameMissing;
    }

    if (m_pluginExpectation == PluginExpectation::NotExpected) {
        return NameAgreement::NoPlugin;
    }

    const std::optional<ModuleMetadata>& embedded = embeddedMetadata();
    if (!embedded || embedded->name.isEmpty()) {
        return NameAgreement::EmbeddedNameMissing;
    }

    return embedded->name == declared ? NameAgreement::Agree : NameAgreement::Disagree;
}

}  // namespace ModuleLib
