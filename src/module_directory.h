#ifndef MODULE_DIRECTORY_H
#define MODULE_DIRECTORY_H

#include "module_metadata.h"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <string>

namespace ModuleLib {

/**
 * @brief What probing one artefact of a module directory found.
 *
 * Absent is a normal state — manifest.sig and `variant` are optional — while
 * Unreadable and Malformed are faults. They are separate values because
 * collapsing them into one empty result is the fail-open pattern that keeps
 * hiding broken installs here.
 */
enum class FileState {
    Present,     ///< read, and parsed where a parsed view is offered
    Absent,      ///< no such path
    Unreadable,  ///< the path exists but could not be opened or read
    Malformed,   ///< read, but not the shape expected of it
};

/**
 * @brief One optional file of a module directory, kept as its exact bytes.
 */
struct ModuleFile {
    FileState state = FileState::Absent;
    /// Exact on-disk bytes. Empty when nothing was read (Absent/Unreadable);
    /// a Malformed file keeps them so a caller can still hash or quote it.
    QByteArray bytes;
    QString error;  ///< why, when state is Unreadable or Malformed

    bool isPresent() const { return state == FileState::Present; }
};

/**
 * @brief manifest.json: its exact bytes, plus a parsed view of that same read.
 *
 * `bytes` is the signed message — a package signature covers the file verbatim
 * and no JSON writer round-trips byte-for-byte, so `parsed` is derived from
 * `bytes` and never the reverse.
 */
struct ManifestFile : ModuleFile {
    QJsonObject parsed;  ///< empty unless state == Present
};

/**
 * @brief Outcome of resolving the manifest's `main` to a file on disk.
 *
 * FileMissing is deliberately not folded into NoVariantMatch: a manifest that
 * names a main this install does not contain is the variant-mismatch case,
 * and a caller wants to say so rather than report "no main declared".
 */
enum class MainResolution {
    Resolved,        ///< the manifest named a main and that file is in the directory
    NoManifest,      ///< no readable, well-formed manifest to consult
    NotDeclared,     ///< the manifest declares no `main`
    MalformedEntry,  ///< `main` is neither object nor string, or names nothing usable
    NoVariantMatch,  ///< `main` is a variant map and no candidate variant is a key
    FileMissing,     ///< `main` named a file that is not in the directory
};

/**
 * @brief The module's main plugin file, as the manifest names it.
 */
struct MainFile {
    MainResolution state = MainResolution::NoManifest;
    /// Absolute path, non-empty only when Resolved, so an unresolved main can
    /// never be mistaken for a usable one.
    QString path;
    QString declaredPath;  ///< the relative path the manifest named, when it named one
    QString variant;       ///< the `main` key that selected declaredPath (map form only)
    QString error;         ///< detail for the non-Resolved states

    bool isResolved() const { return state == MainResolution::Resolved; }
};

/**
 * @brief Whether manifest.json's `name` and the plugin's embedded `name` agree.
 *
 * Reported, never enforced. Refusing a mismatch is the host's policy call
 * (liblogos' impersonation guard); this type only makes the comparison cheap.
 */
enum class NameAgreement {
    Agree,
    Disagree,
    ManifestNameMissing,
    EmbeddedNameMissing,
};

/**
 * @brief An installed module directory — the module IS the directory.
 *
 * An installed module is not a plugin file with sidecars: it is everything in
 * the directory the package manager extracted it into. This type is the one
 * place that knows that layout, so hosts and package managers stop each
 * reimplementing it:
 *
 *     <dir>/manifest.json   the declarative manifest (signed bytes)
 *     <dir>/manifest.sig    the detached signature, when the package was signed
 *     <dir>/variant         the variant that was physically extracted here
 *     <dir>/...             the payload: the plugin plus its private libraries
 *
 * Nothing here is a policy decision. Every probe reports what it found —
 * including "absent", "unreadable" and "malformed" as distinct answers — and
 * the caller decides what to refuse.
 *
 * Example usage:
 * @code
 * ModuleDirectory dir = ModuleDirectory::open(installDir, hostVariants);
 * const QByteArray signedBytes = dir.manifest().bytes;  // verbatim
 * if (dir.main().state == MainResolution::FileMissing) {
 *     qWarning() << "installed for another variant:" << dir.main().declaredPath;
 * }
 * @endcode
 */
class ModuleDirectory {
public:
    /**
     * @brief Probe a module directory. Always returns an object; check state.
     *
     * @param directoryPath      The installed module directory.
     * @param candidateVariants  Variant spellings to accept when `main` is a
     *   variant map, most preferred first.
     *
     * The variant list is CALLER-SUPPLIED on purpose. Variant names have more
     * than one live spelling in this ecosystem (`darwin-x86_64` vs
     * `darwin-amd64`, and the `-dev` suffix a non-portable build appends), the
     * alias table that reconciles them already exists in the package manager,
     * and a second copy here would be a third spelling authority that drifts
     * from both. Pass the host's own list — lgpm's
     * PackageManagerLib::platformVariantsToTry(). This library enumerates no
     * spelling of its own.
     *
     * When the list is empty, the installed `variant` file is the sole
     * candidate: it records exactly what was extracted here, so it resolves
     * this directory without any spelling knowledge at all.
     */
    static ModuleDirectory open(const QString& directoryPath,
                                const QStringList& candidateVariants = QStringList());

    /**
     * @brief Probe a module directory (std::string overload).
     */
    static ModuleDirectory open(const std::string& directoryPath,
                                const std::vector<std::string>& candidateVariants = {});

    /**
     * @brief The directory itself, absolute.
     *
     * Non-empty even when it does not exist, so a diagnostic can name what was
     * looked for.
     */
    const QString& path() const { return m_path; }

    /// Malformed here means the path exists but is not a directory.
    FileState directoryState() const { return m_directoryState; }

    /// True when the path is a readable directory. Everything below is only
    /// meaningful then; otherwise the members report why not.
    bool isValid() const { return m_directoryState == FileState::Present; }

    const ManifestFile& manifest() const { return m_manifest; }

    /// The detached signature over manifest().bytes. Absent for an unsigned
    /// package, which is a normal state and not a fault.
    const ModuleFile& signature() const { return m_signature; }

    const ModuleFile& variantFile() const { return m_variantFile; }

    /// The trimmed contents of `variant`, empty unless that file is Present.
    const QString& installedVariant() const { return m_installedVariant; }

    /// manifest.json's `name`, empty unless the manifest is Present.
    QString manifestName() const;

    /// The resolved main plugin file. Resolution follows the package manager's
    /// rules: first matching candidate variant wins and does not fall through
    /// to a later one when its file is missing.
    const MainFile& main() const { return m_main; }

    /// The variant list this directory was opened with, after the empty-list
    /// fallback to the installed `variant`.
    const QStringList& candidateVariants() const { return m_candidateVariants; }

    /// Directory entries that are not manifest.json, manifest.sig or `variant`
    /// — the plugin and its private libraries. Top level only, sorted by name.
    const QStringList& payloadEntries() const { return m_payloadEntries; }

    /**
     * @brief The plugin metadata embedded in the resolved main.
     *
     * Read with QPluginLoader::metaData(), so the plugin is never instantiated.
     * nullopt when the main did not resolve or carries no metadata. Computed on
     * first call and cached.
     */
    const std::optional<ModuleMetadata>& embeddedMetadata() const;

    /// Compare manifest.json's `name` against the embedded one. A missing
    /// manifest name is reported ahead of a missing embedded one, since the
    /// manifest is the side a host trusts.
    NameAgreement compareNames() const;

private:
    QString m_path;
    FileState m_directoryState = FileState::Absent;
    QStringList m_candidateVariants;
    ManifestFile m_manifest;
    ModuleFile m_signature;
    ModuleFile m_variantFile;
    QString m_installedVariant;
    MainFile m_main;
    QStringList m_payloadEntries;

    mutable bool m_embeddedProbed = false;
    mutable std::optional<ModuleMetadata> m_embedded;
};

}  // namespace ModuleLib

#endif  // MODULE_DIRECTORY_H
