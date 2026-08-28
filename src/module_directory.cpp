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

MainFile locateMain(const QDir& dir, const QString& declaredPath, const QString& variant)
{
    MainFile result;
    result.declaredPath = declaredPath;
    result.variant = variant;

    // The directory IS the module, so a `main` pointing outside it does not
    // name this module's plugin whatever it names. Refuse rather than follow.
    const QString root = QDir::cleanPath(dir.absolutePath());
    const QString candidate = QDir::cleanPath(dir.absoluteFilePath(declaredPath));
    if (candidate != root && !candidate.startsWith(root + QLatin1Char('/'))) {
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
    } else {
        result.m_main.state = MainResolution::NoManifest;
        result.m_main.error = result.m_manifest.error;
    }

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

NameAgreement ModuleDirectory::compareNames() const
{
    const QString declared = manifestName();
    if (declared.isEmpty()) {
        return NameAgreement::ManifestNameMissing;
    }

    const std::optional<ModuleMetadata>& embedded = embeddedMetadata();
    if (!embedded || embedded->name.isEmpty()) {
        return NameAgreement::EmbeddedNameMissing;
    }

    return embedded->name == declared ? NameAgreement::Agree : NameAgreement::Disagree;
}

}  // namespace ModuleLib
