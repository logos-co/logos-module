// ModuleDirectory's checks, which are logos-package's checks reached through
// one door. What is asserted here is that the door opens onto the right room —
// the arithmetic behind each rule is logos-package's own test suite's job.
//
// The two fixtures under tests/examples/ were produced by the lgx CLI, so
// their hashes are genuine:
//
//   installed_signed/    lgx create/add/sign, then `lgx extract`, plus
//                        `lgx manifest --json`, `lgx signature` and a `variant`
//                        file -- exactly what an installer leaves behind.
//   installed_qml_only/  the same, with `main` dropped from the manifest and
//                        type/view/icon set. Content hashes cover the variant's
//                        FILES, so removing a manifest key leaves them valid.
#include <gtest/gtest.h>

#include "module_directory.h"
#include "test_examples_path.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace ModuleLib;

namespace {

// The DID whose key signed installed_signed/manifest.json.
const char* const kSignerDid =
    "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6IkxQMjNNbFI5eWQ4d2VUTk9B"
    "OElEeVNUZ2REdm41cGIxaTMteE5YTm9fWjgifQ";

// A second, unrelated Ed25519 did:jwk. Well-formed, so a check against it fails
// on the signature and not on the DID.
const char* const kOtherDid =
    "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6ImpBazdZc3BXUnk1ckNQcFct"
    "SnItRGlRWHNGOUhJaDhKUVlIVGVZWE9CZ2MifQ";

QString exampleDir(const char* name)
{
    return QDir(QString::fromStdString(testExamplesDir())).filePath(QLatin1String(name));
}

// Every field lgx's parser requires, so a hand-written manifest reaches the
// RULES rather than dying at the parse. `%1` carries what the test is about.
QByteArray manifestWith(const QString& extra)
{
    return QStringLiteral(R"({"manifestVersion": "0.5.0", "name": "m",
        "description": "", "author": "", "type": "core", "category": "",
        "icon": "", "dependencies": [], %1})").arg(extra).toUtf8();
}

bool anyErrorContains(const InstalledChecks& checks, const QString& needle)
{
    for (const QString& error : checks.errors) {
        if (error.contains(needle)) {
            return true;
        }
    }
    return false;
}

}  // namespace

class InstalledChecksTest : public ::testing::Test {
protected:
    QTemporaryDir tmp;

    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        // A fixture that has gone missing must fail the run, never skip it:
        // a skipped check renders as a pass and proves nothing.
        for (const char* fixture : {"installed_signed", "installed_qml_only"}) {
            ASSERT_TRUE(QFileInfo(exampleDir(fixture)).isDir())
                << "fixture missing: " << exampleDir(fixture).toStdString()
                << " (set LOGOS_MODULE_TEST_EXAMPLES)";
        }
    }

    /// A writable copy of a fixture, so a test can damage it.
    QString copyOf(const char* fixture) {
        const QString source = exampleDir(fixture);
        const QString target = tmp.filePath(QLatin1String(fixture));
        copyTree(source, target);
        return target;
    }

    void writeFile(const QString& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }

    QString scratchDir(const char* name) {
        const QString path = tmp.filePath(QLatin1String(name));
        QDir().mkpath(path);
        return path;
    }

private:
    void copyTree(const QString& from, const QString& to) {
        ASSERT_TRUE(QDir().mkpath(to));
        QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString source = it.next();
            const QString target = QDir(to).filePath(QDir(from).relativeFilePath(source));
            ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
            ASSERT_TRUE(QFile::copy(source, target));
        }
    }
};

// =============================================================================
// The checks reach logos-package and come back
// =============================================================================

TEST_F(InstalledChecksTest, AnUntouchedInstallPassesEveryCheck) {
    const InstalledChecks checks =
        ModuleDirectory::open(exampleDir("installed_signed")).checkInstalled();

    EXPECT_TRUE(checks.ran);
    EXPECT_TRUE(checks.valid) << checks.errors.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(checks.integrity, IntegrityState::Ok);
    EXPECT_EQ(checks.variant, QStringLiteral("darwin-arm64"));
    EXPECT_TRUE(checks.errors.isEmpty());
}

TEST_F(InstalledChecksTest, AQmlOnlyUiPluginPassesTheSameChecks) {
    // The case a naive reuse breaks: no `main`, and nothing may complain.
    ModuleDirectory dir = ModuleDirectory::open(exampleDir("installed_qml_only"));
    const InstalledChecks checks = dir.checkInstalled();

    EXPECT_EQ(dir.pluginExpectation(), PluginExpectation::NotExpected);
    EXPECT_TRUE(checks.valid) << checks.errors.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(checks.integrity, IntegrityState::Ok);
    EXPECT_FALSE(anyErrorContains(checks, QStringLiteral("main")));
}

TEST_F(InstalledChecksTest, AnAlteredPayloadFileIsAMismatch) {
    const QString dir = copyOf("installed_signed");
    QFile plugin(QDir(dir).filePath(QStringLiteral("signed_module_plugin.dylib")));
    ASSERT_TRUE(plugin.open(QIODevice::Append));
    plugin.write("x");
    plugin.close();

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_EQ(checks.integrity, IntegrityState::Mismatch);
    EXPECT_FALSE(checks.valid);
    EXPECT_TRUE(anyErrorContains(checks, QStringLiteral("Content hash mismatch")));
}

TEST_F(InstalledChecksTest, AnExtraFileIsAMismatch) {
    const QString dir = copyOf("installed_signed");
    writeFile(QDir(dir).filePath(QStringLiteral("smuggled.dylib")), "payload");

    EXPECT_EQ(ModuleDirectory::open(dir).checkInstalled().integrity,
              IntegrityState::Mismatch);
}

TEST_F(InstalledChecksTest, ADeletedFileIsAMismatch) {
    const QString dir = copyOf("installed_signed");
    ASSERT_TRUE(QFile::remove(QDir(dir).filePath(QStringLiteral("lib/libhelper.dylib"))));

    EXPECT_EQ(ModuleDirectory::open(dir).checkInstalled().integrity,
              IntegrityState::Mismatch);
}

TEST_F(InstalledChecksTest, TheInstallerSidecarsAreNotThemselvesHashed) {
    // manifest.json, manifest.sig and `variant` were never in the hashed
    // listing. Rewriting one must not read as tampering with the payload.
    const QString dir = copyOf("installed_signed");
    writeFile(QDir(dir).filePath(QStringLiteral("variant")), "darwin-arm64\n\n");

    EXPECT_EQ(ModuleDirectory::open(dir).checkInstalled().integrity,
              IntegrityState::Ok);
}

TEST_F(InstalledChecksTest, AVariantTheManifestDeclaresNoHashForIsNotProved) {
    const QString dir = copyOf("installed_signed");
    writeFile(QDir(dir).filePath(QStringLiteral("variant")), "linux-amd64\n");

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_EQ(checks.integrity, IntegrityState::NoHash);
    EXPECT_TRUE(checks.integrityDetail.contains(QStringLiteral("variants/linux-amd64")));
}

TEST_F(InstalledChecksTest, ManifestFieldRulesComeBackVerbatim) {
    const QString dir = scratchDir("bad_manifest");
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")),
              manifestWith(QStringLiteral(
                  R"("version": "1.0", "main": {"darwin-arm64": "../escape.dylib"})")));
    writeFile(QDir(dir).filePath(QStringLiteral("variant")), "darwin-arm64\n");

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_TRUE(checks.ran);
    EXPECT_FALSE(checks.valid);
    EXPECT_TRUE(anyErrorContains(
        checks, QStringLiteral("Manifest: 'version' is not a valid SemVer 2.0.0 version")));
    EXPECT_TRUE(anyErrorContains(
        checks, QStringLiteral("Manifest: Invalid main path for 'darwin-arm64'")));
}

TEST_F(InstalledChecksTest, AMainTheDirectoryDoesNotHaveIsReported) {
    const QString dir = scratchDir("no_main_file");
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")),
              manifestWith(QStringLiteral(
                  R"("version": "1.0.0", "main": {"darwin-arm64": "m.dylib"})")));
    writeFile(QDir(dir).filePath(QStringLiteral("variant")), "darwin-arm64\n");

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_TRUE(anyErrorContains(
        checks, QStringLiteral("main[darwin-arm64] points to non-existent file: m.dylib")));
}

TEST_F(InstalledChecksTest, AUiPluginsMissingViewIsReported) {
    const QString dir = copyOf("installed_qml_only");
    ASSERT_TRUE(QFile::remove(QDir(dir).filePath(QStringLiteral("qml/Main.qml"))));

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_TRUE(anyErrorContains(
        checks, QStringLiteral("view file missing for variant 'darwin-arm64': qml/Main.qml")));
}

TEST_F(InstalledChecksTest, AUiPluginsIconContractIsEnforced) {
    // The icon is read from the canonical assets/icon.png and must be exactly
    // 256x256 for a 0.4.0+ package. A one-byte PNG is neither.
    const QString dir = copyOf("installed_qml_only");
    writeFile(QDir(dir).filePath(QStringLiteral("assets/icon.png")), "not a png");

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_FALSE(checks.valid);
    EXPECT_TRUE(anyErrorContains(checks, QStringLiteral("icon")));
}

TEST_F(InstalledChecksTest, JsonThatIsNotAManifestIsBadInputRatherThanNoHash) {
    // Valid JSON, so this library reads it; not a manifest, so logos-package
    // cannot. The caller's input is what is wrong, and the verdict says so
    // instead of "this package simply declares no hash".
    const QString dir = scratchDir("json_but_not_a_manifest");
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")), R"({"name": "m"})");
    writeFile(QDir(dir).filePath(QStringLiteral("variant")), "darwin-arm64\n");

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_TRUE(checks.ran);
    EXPECT_EQ(checks.integrity, IntegrityState::BadInput);
    EXPECT_TRUE(anyErrorContains(checks, QStringLiteral("Failed to parse manifest")));
}

// =============================================================================
// The checks that could not run at all
// =============================================================================

TEST_F(InstalledChecksTest, WithNoManifestNothingCanBeChecked) {
    const QString dir = scratchDir("empty");

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_FALSE(checks.ran);
    EXPECT_FALSE(checks.valid);
    EXPECT_TRUE(anyErrorContains(checks, QStringLiteral("no manifest.json")));
}

TEST_F(InstalledChecksTest, APathThatIsNotADirectorySaysThat) {
    const QString file = tmp.filePath(QStringLiteral("a_file"));
    writeFile(file, "x");

    EXPECT_TRUE(anyErrorContains(ModuleDirectory::open(file).checkInstalled(),
                                 QStringLiteral("not a directory")));
    EXPECT_TRUE(anyErrorContains(
        ModuleDirectory::open(tmp.filePath(QStringLiteral("nope"))).checkInstalled(),
        QStringLiteral("no such directory")));
}

TEST_F(InstalledChecksTest, AMalformedManifestSaysSoRatherThanFailingToParse) {
    const QString dir = scratchDir("broken_json");
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")), "{ not json");

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_FALSE(checks.ran);
    EXPECT_TRUE(anyErrorContains(checks, QStringLiteral("manifest.json is not valid JSON")));
}

TEST_F(InstalledChecksTest, WithNoVariantThereIsNothingToCheckAgainst) {
    const QString dir = scratchDir("no_variant");
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")),
              manifestWith(QStringLiteral(
                  R"("version": "1.0.0", "view": "qml/Main.qml")")));

    const InstalledChecks checks = ModuleDirectory::open(dir).checkInstalled();

    EXPECT_FALSE(checks.ran);
    EXPECT_TRUE(anyErrorContains(checks, QStringLiteral("no installed variant")));
}

// =============================================================================
// Signatures, against a DID the caller names
// =============================================================================

TEST_F(InstalledChecksTest, ThePinnedDidVerifies) {
    EXPECT_EQ(ModuleDirectory::open(exampleDir("installed_signed"))
                  .checkSignature(QLatin1String(kSignerDid)),
              SignatureCheck::Ok);
}

TEST_F(InstalledChecksTest, ADifferentDidIsAMismatch) {
    EXPECT_EQ(ModuleDirectory::open(exampleDir("installed_signed"))
                  .checkSignature(QLatin1String(kOtherDid)),
              SignatureCheck::Mismatch);
}

TEST_F(InstalledChecksTest, TheDidInsideTheDocumentIsNeverTheKey) {
    // Relabel the signature: keep the 64 signature bytes, swap in another
    // party's DID. Reading the key out of the document would make this pass.
    const QString dir = copyOf("installed_signed");
    const QString sigPath = QDir(dir).filePath(QStringLiteral("manifest.sig"));
    QFile sig(sigPath);
    ASSERT_TRUE(sig.open(QIODevice::ReadOnly));
    QByteArray json = sig.readAll();
    sig.close();
    json.replace(kSignerDid, kOtherDid);
    writeFile(sigPath, json);

    ModuleDirectory relabelled = ModuleDirectory::open(dir);

    ASSERT_EQ(relabelled.claimedSignerDid(), QLatin1String(kOtherDid));
    EXPECT_EQ(relabelled.checkSignature(QLatin1String(kOtherDid)), SignatureCheck::Mismatch);
    // The original signer still verifies: only the label moved.
    EXPECT_EQ(relabelled.checkSignature(QLatin1String(kSignerDid)), SignatureCheck::Ok);
}

TEST_F(InstalledChecksTest, EditingTheSignedManifestBreaksTheSignatureAlone) {
    // The hashes cover the payload FILES, so an edit to manifest.json leaves
    // integrity intact and shows up only where it should.
    const QString dir = copyOf("installed_signed");
    const QString manifestPath = QDir(dir).filePath(QStringLiteral("manifest.json"));
    QFile manifest(manifestPath);
    ASSERT_TRUE(manifest.open(QIODevice::ReadOnly));
    QByteArray json = manifest.readAll();
    manifest.close();
    json.replace("\"description\": \"\"", "\"description\": \"x\"");
    writeFile(manifestPath, json);

    ModuleDirectory edited = ModuleDirectory::open(dir);

    EXPECT_EQ(edited.checkInstalled().integrity, IntegrityState::Ok);
    EXPECT_EQ(edited.checkSignature(QLatin1String(kSignerDid)), SignatureCheck::Mismatch);
}

TEST_F(InstalledChecksTest, ACallersDidThatIsNotADidJwkIsTheCallersError) {
    EXPECT_EQ(ModuleDirectory::open(exampleDir("installed_signed"))
                  .checkSignature(QStringLiteral("did:web:example.com")),
              SignatureCheck::BadDid);
    EXPECT_EQ(ModuleDirectory::open(exampleDir("installed_signed"))
                  .checkSignature(QString()),
              SignatureCheck::BadDid);
}

TEST_F(InstalledChecksTest, AnUnsignedPackageSaysUnsigned) {
    EXPECT_EQ(ModuleDirectory::open(exampleDir("installed_qml_only"))
                  .checkSignature(QLatin1String(kSignerDid)),
              SignatureCheck::Unsigned);
}

TEST_F(InstalledChecksTest, AnUnusableSignatureDocumentIsNotAMismatch) {
    const QString dir = copyOf("installed_signed");
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.sig")), "{ not json");

    EXPECT_EQ(ModuleDirectory::open(dir).checkSignature(QLatin1String(kSignerDid)),
              SignatureCheck::Unusable);
}

TEST_F(InstalledChecksTest, WithNoManifestThereIsNoSignedMessage) {
    // A definitive Mismatch here would be a statement about bytes that do not
    // exist, which is why this state is its own.
    const QString dir = scratchDir("sig_only");
    writeFile(QDir(dir).filePath(QStringLiteral("manifest.sig")), "{}");

    EXPECT_EQ(ModuleDirectory::open(dir).checkSignature(QLatin1String(kSignerDid)),
              SignatureCheck::NoMessage);
}

TEST_F(InstalledChecksTest, TheClaimedDidIsReadableWithoutBeingTrusted) {
    ModuleDirectory dir = ModuleDirectory::open(exampleDir("installed_signed"));

    EXPECT_EQ(dir.claimedSignerDid(), QLatin1String(kSignerDid));
    EXPECT_TRUE(ModuleDirectory::open(exampleDir("installed_qml_only"))
                    .claimedSignerDid().isEmpty());
}
