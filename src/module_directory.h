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
 * @brief What the manifest's `type` makes of this directory.
 *
 * The one distinction the layout turns on: a `ui_qml` package is a QML view
 * that MAY be backed by a plugin, so declaring no `main` is legal for it and
 * broken for everything else. Every other type is its plugin.
 */
enum class ModuleKind {
    Core,      ///< the plugin IS the module; a main is mandatory
    UiPlugin,  ///< `ui_qml`: a QML view, optionally backed by a plugin
    Unknown,   ///< the manifest declares no type, or one with no rule here
};

/**
 * @brief Outcome of resolving a path the manifest names — `view`, `icon`.
 *
 * Shaped like MainResolution for the same reason: "not declared" and "declared
 * but not here" are different repairs and must not share a value.
 */
enum class AssetResolution {
    Resolved,       ///< the manifest named it and that file is in the directory
    NotDeclared,    ///< the manifest names none
    FileMissing,    ///< named, but not in the directory
    OutsideModule,  ///< the named path escapes the directory; never opened
};

/**
 * @brief A file the manifest names by relative path.
 */
struct AssetFile {
    AssetResolution state = AssetResolution::NotDeclared;
    QString declaredPath;  ///< the relative path as the manifest wrote it
    QString path;          ///< absolute, non-empty only when Resolved
    QString error;

    bool isResolved() const { return state == AssetResolution::Resolved; }
};

/**
 * @brief Whether this package is supposed to have a plugin at all.
 *
 * MainFile says what resolution FOUND; this says whether the finding is a
 * fault, which only the manifest's `type` can decide. A QML-only ui_qml
 * package with no plugin is complete; a core module with no plugin is broken.
 * Keeping them apart is what stops "be permissive for ui_qml" from leaking
 * into every caller that reads MainFile.
 */
enum class PluginExpectation {
    Present,      ///< a main resolved: there is a plugin to load
    Missing,      ///< no plugin resolved, and this package needs one
    NotExpected,  ///< a view-only ui_qml package legitimately has none
};

/**
 * @brief Whether the installed files are still the ones the manifest covers.
 *
 * lgx_integrity_t verbatim: only Ok is a pass, and the three non-Mismatch
 * values are each "not a verdict" for a different reason.
 */
enum class IntegrityState {
    Ok,          ///< the tree hashes to the manifest's value for this variant
    Mismatch,    ///< it does not: content added, removed or altered
    NoHash,      ///< no hash declared for this variant; nothing proved either way
    Unreadable,  ///< the check could not run
    BadInput,    ///< there was no usable variant or manifest to check against
};

/**
 * @brief What logos-package makes of this installed directory.
 *
 * Findings, not a policy: `valid` is the sum of the checks that ran, and every
 * caller decides for itself what to refuse. `ran` is false when this library
 * could not get as far as calling logos-package — a missing manifest, no
 * variant to check — and `errors` then says which.
 */
struct InstalledChecks {
    bool ran = false;
    bool valid = false;
    IntegrityState integrity = IntegrityState::BadInput;
    QString integrityDetail;  ///< human-readable reason when integrity is not Ok
    QString variant;          ///< the variant the checks ran against
    QStringList errors;
    QStringList warnings;
};

/**
 * @brief Whether a DID THE CALLER NAMED signed this directory's manifest.
 */
enum class SignatureCheck {
    Ok,        ///< the pinned DID's key produced manifest.sig over manifest().bytes
    Mismatch,  ///< a usable signature the pinned DID did not produce
    Unusable,  ///< manifest.sig is present but unparseable or not an Ed25519 signature
    BadDid,    ///< the CALLER's DID is not a did:jwk carrying an Ed25519 key
    Unsigned,  ///< there is no manifest.sig here
    /// There is no readable manifest.json — no signed message to check
    /// anything against. Distinct from Mismatch, which would otherwise be the
    /// answer, and would be a definitive statement about bytes that do not exist.
    NoMessage,
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
    /// There is no plugin here to carry a name, and none is supposed to be —
    /// a view-only ui_qml package. Kept apart from EmbeddedNameMissing, which
    /// is a plugin that exists and left its name blank.
    NoPlugin,
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
 * A UI plugin is the same directory with the same manifest, so it is opened
 * the same way. What it adds — `view`, `icon`, and permission to carry no
 * plugin at all — is reported by members every directory has, and no caller
 * branches on the type to read them.
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

    /// manifest.json's `type` verbatim, empty unless the manifest is Present.
    QString declaredType() const;

    /// declaredType() reduced to the one rule this layout has for it.
    ModuleKind kind() const;

    /// The resolved main plugin file. Resolution follows the package manager's
    /// rules: first matching candidate variant wins and does not fall through
    /// to a later one when its file is missing.
    const MainFile& main() const { return m_main; }

    /// Whether a plugin is expected here at all. Read this, not main(), to
    /// decide whether a directory is broken.
    PluginExpectation pluginExpectation() const { return m_pluginExpectation; }

    /// The QML entry point, from the manifest's `view`. NotDeclared for
    /// anything that is not a ui_qml package — so no caller needs to test the
    /// type before asking.
    const AssetFile& view() const { return m_view; }

    /// The package icon, from the manifest's `icon`. Resolved against this
    /// directory, which answers "is the file the manifest names here?" — a
    /// different question from the 0.4.0 icon CONTRACT (exact dimensions at
    /// assets/icon.png), which checkInstalled() gets from logos-package.
    const AssetFile& icon() const { return m_icon; }

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
     * nullopt when the main did not resolve or carries no metadata — including
     * the ordinary case of a QML-only ui_qml package, which has no binary at
     * all. Check pluginExpectation() to tell that apart from a fault. Computed
     * on first call and cached.
     */
    const std::optional<ModuleMetadata>& embeddedMetadata() const;

    /// Compare manifest.json's `name` against the embedded one. A missing
    /// manifest name is reported ahead of a missing embedded one, since the
    /// manifest is the side a host trusts.
    NameAgreement compareNames() const;

    /**
     * @brief The variant the installed-directory checks run against.
     *
     * The `variant` sidecar when there is one — it records exactly what was
     * extracted here — otherwise the candidate that resolved `main`. Empty
     * when neither says, which is the one case checkInstalled() cannot run.
     */
    QString checkedVariant() const;

    /**
     * @brief Run every check that survives installation, through logos-package.
     *
     * The manifest's own field rules, the integrity of the tree against
     * hashes["variants/<variant>"], main/view resolution and the icon
     * contract. All of it is logos-package's code, reached through its C ABI:
     * this library owns the layout, that one owns the format.
     *
     * Reports; never refuses. Not cached — it hashes every file in the
     * directory, and a caller that wants one answer should keep it.
     */
    InstalledChecks checkInstalled() const;

    /**
     * @brief Check manifest.sig against a DID the CALLER pins.
     *
     * `expectedDid` is required and there is deliberately no overload without
     * one. Taking the DID out of manifest.sig and verifying with that same
     * document's key proves only that the document is internally consistent:
     * whoever replaces the signature replaces the DID beside it. A pin — or a
     * DID the caller looked up in its own keyring — is the only input that
     * makes the answer mean anything.
     */
    SignatureCheck checkSignature(const QString& expectedDid) const;

    /**
     * @brief The DID manifest.sig CLAIMS to have been signed by.
     *
     * Self-asserted, and never used as a key. It exists so a human can see
     * what to pin; passing it straight back into checkSignature() proves
     * nothing at all.
     */
    QString claimedSignerDid() const;

private:
    QString m_path;
    FileState m_directoryState = FileState::Absent;
    QStringList m_candidateVariants;
    ManifestFile m_manifest;
    ModuleFile m_signature;
    ModuleFile m_variantFile;
    QString m_installedVariant;
    MainFile m_main;
    AssetFile m_view;
    AssetFile m_icon;
    PluginExpectation m_pluginExpectation = PluginExpectation::Missing;
    QStringList m_payloadEntries;

    PluginExpectation computePluginExpectation() const;

    mutable bool m_embeddedProbed = false;
    mutable std::optional<ModuleMetadata> m_embedded;
};

}  // namespace ModuleLib

#endif  // MODULE_DIRECTORY_H
