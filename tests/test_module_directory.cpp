#include <gtest/gtest.h>

#include "module_directory.h"
#include "module_metadata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace ModuleLib;

namespace {

const char* const kManifestWithMap = R"({
  "name": "accounts_module",
  "version": "1.0.1",
  "type": "core",
  "main": {
    "darwin-arm64": "accounts_module_plugin.dylib",
    "linux-amd64": "accounts_module_plugin.so"
  }
}
)";

}  // namespace

// =============================================================================
// Fixture: builds a module directory file by file
// =============================================================================

class ModuleDirectoryTest : public ::testing::Test {
protected:
    QTemporaryDir tmp;

    void SetUp() override {
        ASSERT_TRUE(tmp.isValid()) << "could not create a temporary directory";
    }

    QString moduleDir() const { return tmp.filePath(QStringLiteral("my_module")); }

    void makeModuleDir() { ASSERT_TRUE(QDir().mkpath(moduleDir())); }

    void writeFile(const QString& name, const QByteArray& bytes) {
        QFile file(QDir(moduleDir()).filePath(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
        file.close();
    }
};

// =============================================================================
// Directory state: absent / not a directory / present
// =============================================================================

TEST_F(ModuleDirectoryTest, Absent_ReportsAbsentAndNamesThePath) {
    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_FALSE(dir.isValid());
    EXPECT_EQ(dir.directoryState(), FileState::Absent);
    EXPECT_FALSE(dir.path().isEmpty());
    EXPECT_EQ(dir.manifest().state, FileState::Absent);
    EXPECT_EQ(dir.main().state, MainResolution::NoManifest);
}

TEST_F(ModuleDirectoryTest, PathIsAFile_ReportsMalformedNotAbsent) {
    QFile file(moduleDir());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_FALSE(dir.isValid());
    EXPECT_EQ(dir.directoryState(), FileState::Malformed);
}

#ifndef _WIN32
TEST_F(ModuleDirectoryTest, UnreadableDirectory_ReportsUnknownNotAbsent) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    ASSERT_EQ(::chmod(moduleDir().toLocal8Bit().constData(), 0000), 0);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());
    const bool blocked = dir.directoryState() == FileState::Unreadable;
    ::chmod(moduleDir().toLocal8Bit().constData(), 0700);

    // Running as root defeats the mode bits; only assert when they held.
    ASSERT_TRUE(blocked) << "directory mode 0000 did not block the read (running as root?)";
    EXPECT_EQ(dir.manifest().state, FileState::Unreadable);
    EXPECT_EQ(dir.signature().state, FileState::Unreadable);
    EXPECT_EQ(dir.variantFile().state, FileState::Unreadable);
}
#endif

// =============================================================================
// manifest.json: exact bytes, parsed view, and the three failure states
// =============================================================================

TEST_F(ModuleDirectoryTest, Manifest_BytesAreExactlyWhatIsOnDisk) {
    // Key order, indentation and the trailing newline are all preserved: these
    // bytes are the signed message, so any re-serialisation breaks signatures.
    const QByteArray onDisk =
        "{\n  \"version\" : \"9.9.9\",\n\t\"name\":\"my_module\"  ,\n \"type\": \"core\"\n}\n";
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), onDisk);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    ASSERT_EQ(dir.manifest().state, FileState::Present);
    EXPECT_EQ(dir.manifest().bytes, onDisk);

    // Guard the guard: a re-dump of the parsed object really is different, so
    // this test would catch a derived-from-parsed implementation.
    const QByteArray redumped = QJsonDocument(dir.manifest().parsed).toJson();
    EXPECT_NE(redumped, onDisk);

    EXPECT_EQ(dir.manifestName(), QStringLiteral("my_module"));
    EXPECT_EQ(dir.manifest().parsed.value(QStringLiteral("version")).toString(),
              QStringLiteral("9.9.9"));
}

TEST_F(ModuleDirectoryTest, Manifest_AbsentIsDistinctFromMalformed) {
    makeModuleDir();

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_TRUE(dir.isValid());
    EXPECT_EQ(dir.manifest().state, FileState::Absent);
    EXPECT_TRUE(dir.manifest().bytes.isEmpty());
    EXPECT_TRUE(dir.manifestName().isEmpty());
}

TEST_F(ModuleDirectoryTest, Manifest_InvalidJsonIsMalformedAndKeepsItsBytes) {
    const QByteArray onDisk = "{ \"name\": \"my_module\", ";
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), onDisk);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.manifest().state, FileState::Malformed);
    EXPECT_FALSE(dir.manifest().error.isEmpty());
    EXPECT_EQ(dir.manifest().bytes, onDisk);
    EXPECT_TRUE(dir.manifest().parsed.isEmpty());
    EXPECT_EQ(dir.main().state, MainResolution::NoManifest);
}

TEST_F(ModuleDirectoryTest, Manifest_JsonArrayIsMalformed) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), "[\"my_module\"]");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.manifest().state, FileState::Malformed);
}

#ifndef _WIN32
TEST_F(ModuleDirectoryTest, Manifest_UnreadableIsDistinctFromAbsent) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    const QString manifestPath = QDir(moduleDir()).filePath(QStringLiteral("manifest.json"));
    ASSERT_EQ(::chmod(manifestPath.toLocal8Bit().constData(), 0000), 0);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());
    const FileState state = dir.manifest().state;
    ::chmod(manifestPath.toLocal8Bit().constData(), 0600);

    ASSERT_EQ(state, FileState::Unreadable) << "file mode 0000 did not block the read (root?)";
    EXPECT_FALSE(dir.manifest().error.isEmpty());
}
#endif

// =============================================================================
// manifest.sig: absent is normal, present is exact bytes
// =============================================================================

TEST_F(ModuleDirectoryTest, Signature_AbsentIsANormalStateWithNoError) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.signature().state, FileState::Absent);
    EXPECT_TRUE(dir.signature().error.isEmpty());
    EXPECT_FALSE(dir.signature().isPresent());
}

TEST_F(ModuleDirectoryTest, Signature_PresentIsExactBytes) {
    const QByteArray sig = "{\"alg\":\"Ed25519\",\"sig\":\"Zm9vYmFy\"}\n";
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    writeFile(QStringLiteral("manifest.sig"), sig);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.signature().state, FileState::Present);
    EXPECT_EQ(dir.signature().bytes, sig);
    EXPECT_FALSE(dir.payloadEntries().contains(QStringLiteral("manifest.sig")));
}

// =============================================================================
// variant
// =============================================================================

TEST_F(ModuleDirectoryTest, Variant_PresentIsTrimmed) {
    makeModuleDir();
    writeFile(QStringLiteral("variant"), "darwin-arm64\r\n");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.variantFile().state, FileState::Present);
    EXPECT_EQ(dir.installedVariant(), QStringLiteral("darwin-arm64"));
}

TEST_F(ModuleDirectoryTest, Variant_AbsentIsANormalState) {
    makeModuleDir();

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.variantFile().state, FileState::Absent);
    EXPECT_TRUE(dir.installedVariant().isEmpty());
}

TEST_F(ModuleDirectoryTest, Variant_BlankFileIsMalformedNotAbsent) {
    makeModuleDir();
    writeFile(QStringLiteral("variant"), "\n");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.variantFile().state, FileState::Malformed);
    EXPECT_TRUE(dir.installedVariant().isEmpty());
}

// =============================================================================
// main resolution
// =============================================================================

TEST_F(ModuleDirectoryTest, Main_VariantMapResolvesAgainstCallerSuppliedVariants) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    writeFile(QStringLiteral("accounts_module_plugin.dylib"), "MACHO");

    ModuleDirectory dir =
        ModuleDirectory::open(moduleDir(), {QStringLiteral("darwin-arm64")});

    ASSERT_TRUE(dir.main().isResolved());
    EXPECT_EQ(dir.main().variant, QStringLiteral("darwin-arm64"));
    EXPECT_EQ(dir.main().declaredPath, QStringLiteral("accounts_module_plugin.dylib"));
    EXPECT_EQ(dir.main().path,
              QDir(dir.path()).filePath(QStringLiteral("accounts_module_plugin.dylib")));
}

TEST_F(ModuleDirectoryTest, Main_EmptyVariantListFallsBackToTheInstalledVariant) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    writeFile(QStringLiteral("accounts_module_plugin.dylib"), "MACHO");
    writeFile(QStringLiteral("variant"), "darwin-arm64\n");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.candidateVariants(), QStringList{QStringLiteral("darwin-arm64")});
    ASSERT_TRUE(dir.main().isResolved());
    EXPECT_EQ(dir.main().variant, QStringLiteral("darwin-arm64"));
}

TEST_F(ModuleDirectoryTest, Main_PlainStringForm) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"),
              R"({"name":"my_module","main":"my_module_plugin.so"})");
    writeFile(QStringLiteral("my_module_plugin.so"), "ELF");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    ASSERT_TRUE(dir.main().isResolved());
    EXPECT_EQ(dir.main().declaredPath, QStringLiteral("my_module_plugin.so"));
    EXPECT_TRUE(dir.main().variant.isEmpty());
}

TEST_F(ModuleDirectoryTest, Main_NamedFileMissingIsFileMissingNotNoVariantMatch) {
    // The variant-mismatch case: the manifest names a main this install does
    // not contain. Both reasons collapse to "" in the package manager.
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);

    ModuleDirectory dir =
        ModuleDirectory::open(moduleDir(), {QStringLiteral("darwin-arm64")});

    EXPECT_EQ(dir.main().state, MainResolution::FileMissing);
    EXPECT_EQ(dir.main().declaredPath, QStringLiteral("accounts_module_plugin.dylib"));
    EXPECT_TRUE(dir.main().path.isEmpty());
    EXPECT_FALSE(dir.main().error.isEmpty());
}

TEST_F(ModuleDirectoryTest, Main_NoCandidateVariantIsAKey) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);

    ModuleDirectory dir =
        ModuleDirectory::open(moduleDir(), {QStringLiteral("windows-x86_64")});

    EXPECT_EQ(dir.main().state, MainResolution::NoVariantMatch);
    EXPECT_TRUE(dir.main().declaredPath.isEmpty());
}

TEST_F(ModuleDirectoryTest, Main_FirstMatchingVariantWinsAndDoesNotFallThrough) {
    // Package-manager parity: a matched variant whose file is missing loses
    // outright rather than handing the resolve to the next candidate.
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    writeFile(QStringLiteral("accounts_module_plugin.so"), "ELF");

    ModuleDirectory dir = ModuleDirectory::open(
        moduleDir(), {QStringLiteral("darwin-arm64"), QStringLiteral("linux-amd64")});

    EXPECT_EQ(dir.main().state, MainResolution::FileMissing);
    EXPECT_EQ(dir.main().variant, QStringLiteral("darwin-arm64"));
}

TEST_F(ModuleDirectoryTest, Main_NotDeclared) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), R"({"name":"my_module","version":"1.0.0"})");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::NotDeclared);
}

TEST_F(ModuleDirectoryTest, Main_WrongTypeIsMalformedEntry) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), R"({"name":"my_module","main":42})");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::MalformedEntry);
    EXPECT_FALSE(dir.main().error.isEmpty());
}

TEST_F(ModuleDirectoryTest, Main_EmptyStringIsMalformedEntry) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), R"({"name":"my_module","main":""})");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::MalformedEntry);
}

TEST_F(ModuleDirectoryTest, Main_UnusableMapValueIsMalformedEntryNotNoVariantMatch) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"),
              R"({"name":"my_module","main":{"darwin-arm64":""}})");

    ModuleDirectory dir =
        ModuleDirectory::open(moduleDir(), {QStringLiteral("darwin-arm64")});

    EXPECT_EQ(dir.main().state, MainResolution::MalformedEntry);
    EXPECT_EQ(dir.main().variant, QStringLiteral("darwin-arm64"));
}

TEST_F(ModuleDirectoryTest, Main_UnusableValueStillLetsALaterVariantWin) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"),
              R"({"name":"my_module","main":{"darwin-arm64":"","linux-amd64":"p.so"}})");
    writeFile(QStringLiteral("p.so"), "ELF");

    ModuleDirectory dir = ModuleDirectory::open(
        moduleDir(), {QStringLiteral("darwin-arm64"), QStringLiteral("linux-amd64")});

    ASSERT_TRUE(dir.main().isResolved());
    EXPECT_EQ(dir.main().variant, QStringLiteral("linux-amd64"));
}

TEST_F(ModuleDirectoryTest, Main_NamingADirectoryIsNotResolved) {
    // exists() is true for a directory, which would hand the caller a path it
    // can never load.
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"),
              R"({"name":"my_module","main":"not_a_plugin"})");
    ASSERT_TRUE(QDir(moduleDir()).mkdir(QStringLiteral("not_a_plugin")));

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_NE(dir.main().state, MainResolution::Resolved);
    EXPECT_EQ(dir.main().state, MainResolution::MalformedEntry);
    EXPECT_TRUE(dir.main().path.isEmpty());
}

TEST_F(ModuleDirectoryTest, Main_SymlinkToAFileStillResolves) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"),
              R"({"name":"my_module","main":"link_plugin.so"})");
    writeFile(QStringLiteral("real_plugin.so"), "x");
    ASSERT_TRUE(QFile::link(QDir(moduleDir()).absoluteFilePath(QStringLiteral("real_plugin.so")),
                            QDir(moduleDir()).absoluteFilePath(QStringLiteral("link_plugin.so"))));

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::Resolved);
}

TEST_F(ModuleDirectoryTest, Main_EscapingTheDirectoryIsRefused) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"),
              R"({"name":"my_module","main":"../outside_plugin.so"})");
    QFile outside(tmp.filePath(QStringLiteral("outside_plugin.so")));
    ASSERT_TRUE(outside.open(QIODevice::WriteOnly));
    outside.close();

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::MalformedEntry);
    EXPECT_TRUE(dir.main().path.isEmpty());
}

// =============================================================================
// payload entries
// =============================================================================

TEST_F(ModuleDirectoryTest, Payload_ExcludesTheThreeMetadataFiles) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    writeFile(QStringLiteral("manifest.sig"), "sig");
    writeFile(QStringLiteral("variant"), "darwin-arm64\n");
    writeFile(QStringLiteral("accounts_module_plugin.dylib"), "MACHO");
    writeFile(QStringLiteral("libssl.3.dylib"), "MACHO");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    const QStringList expected{QStringLiteral("accounts_module_plugin.dylib"),
                               QStringLiteral("libssl.3.dylib")};
    EXPECT_EQ(dir.payloadEntries(), expected);
}

// =============================================================================
// embedded metadata and the name comparison
// =============================================================================

TEST_F(ModuleDirectoryTest, EmbeddedMetadata_NulloptWhenMainDidNotResolve) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);

    ModuleDirectory dir =
        ModuleDirectory::open(moduleDir(), {QStringLiteral("darwin-arm64")});

    EXPECT_FALSE(dir.embeddedMetadata().has_value());
    EXPECT_EQ(dir.compareNames(), NameAgreement::EmbeddedNameMissing);
}

TEST_F(ModuleDirectoryTest, CompareNames_MissingManifestNameIsReportedFirst) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), R"({"version":"1.0.0"})");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.compareNames(), NameAgreement::ManifestNameMissing);
}

// =============================================================================
// std::string overload
// =============================================================================

TEST_F(ModuleDirectoryTest, StdStringOverload_MatchesTheQStringOne) {
    makeModuleDir();
    writeFile(QStringLiteral("manifest.json"), kManifestWithMap);
    writeFile(QStringLiteral("accounts_module_plugin.so"), "ELF");

    ModuleDirectory viaQString =
        ModuleDirectory::open(moduleDir(), {QStringLiteral("linux-amd64")});
    ModuleDirectory viaStdString =
        ModuleDirectory::open(moduleDir().toStdString(), {std::string("linux-amd64")});

    EXPECT_EQ(viaQString.path(), viaStdString.path());
    EXPECT_EQ(viaQString.manifest().bytes, viaStdString.manifest().bytes);
    EXPECT_EQ(viaQString.main().path, viaStdString.main().path);
    EXPECT_TRUE(viaStdString.main().isResolved());
}

// =============================================================================
// A real plugin as the main: embedded metadata, and the name comparison
//
// Shares test_metadata.cpp's TEST_PLUGIN convention. QPluginLoader::metaData()
// reads the embedded blob without instantiating, so this works for the
// pre-built example plugin of either architecture.
// =============================================================================

class ModuleDirectoryPluginTest : public ModuleDirectoryTest {
protected:
    std::string testPlugin;

    void SetUp() override {
        ModuleDirectoryTest::SetUp();

        const char* envPlugin = std::getenv("TEST_PLUGIN");
        if (envPlugin && std::string(envPlugin).length() > 0) {
            testPlugin = envPlugin;
        } else {
#ifdef __APPLE__
            const std::string pluginName = "package_manager_plugin.dylib";
#else
            const std::string pluginName = "package_manager_plugin.so";
#endif
            const QStringList prefixes{QStringLiteral("tests/examples/"),
                                       QStringLiteral("../tests/examples/"),
                                       QStringLiteral("../../tests/examples/"),
                                       QStringLiteral("examples/")};
            for (const QString& prefix : prefixes) {
                const QString candidate = prefix + QString::fromStdString(pluginName);
                if (QFileInfo::exists(candidate)) {
                    testPlugin = candidate.toStdString();
                    break;
                }
            }
        }

        if (testPlugin.empty()) {
            GTEST_SKIP() << "Test plugin not found. Set TEST_PLUGIN environment variable.";
        }
    }

    // Copy the plugin in under a manifest that names it as the main.
    void installPluginAs(const QString& manifestName) {
        makeModuleDir();
        const QString target = QDir(moduleDir()).filePath(QStringLiteral("main_plugin"));
        ASSERT_TRUE(QFile::copy(QString::fromStdString(testPlugin), target));

        QJsonObject manifest;
        manifest[QStringLiteral("name")] = manifestName;
        manifest[QStringLiteral("main")] = QStringLiteral("main_plugin");
        writeFile(QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson());
    }
};

TEST_F(ModuleDirectoryPluginTest, EmbeddedMetadata_ReadFromTheResolvedMain) {
    installPluginAs(QStringLiteral("package_manager"));

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    ASSERT_TRUE(dir.main().isResolved());
    ASSERT_TRUE(dir.embeddedMetadata().has_value());
    EXPECT_EQ(dir.embeddedMetadata()->name, QStringLiteral("package_manager"));
    EXPECT_EQ(dir.compareNames(), NameAgreement::Agree);
}

TEST_F(ModuleDirectoryPluginTest, CompareNames_SurfacesAnImpersonatingManifest) {
    installPluginAs(QStringLiteral("innocuous_module"));

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    ASSERT_TRUE(dir.main().isResolved());
    EXPECT_EQ(dir.compareNames(), NameAgreement::Disagree);
    // Reported, not enforced — both names stay available to the caller.
    EXPECT_EQ(dir.manifestName(), QStringLiteral("innocuous_module"));
    EXPECT_EQ(dir.embeddedMetadata()->name, QStringLiteral("package_manager"));
}

// =============================================================================
// UI plugins: the same directory, the same manifest, one extra rule
// =============================================================================

namespace {

const char* const kUiQmlQmlOnly = R"({
  "name": "accounts_ui",
  "version": "1.0.1",
  "type": "ui_qml",
  "view": "qml/AccountsView.qml",
  "icon": "accounts.png"
}
)";

}  // namespace

class UiPluginDirectoryTest : public ModuleDirectoryTest {
protected:
    void writeIn(const QString& relative, const QByteArray& bytes) {
        const QString target = QDir(moduleDir()).filePath(relative);
        ASSERT_TRUE(QDir().mkpath(QFileInfo(target).absolutePath()));
        QFile file(target);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }
};

TEST_F(UiPluginDirectoryTest, KindComesFromTheManifestType) {
    makeModuleDir();
    writeFile("manifest.json", kUiQmlQmlOnly);
    EXPECT_EQ(ModuleDirectory::open(moduleDir()).kind(), ModuleKind::UiPlugin);
    EXPECT_EQ(ModuleDirectory::open(moduleDir()).declaredType(), QStringLiteral("ui_qml"));

    writeFile("manifest.json", kManifestWithMap);
    EXPECT_EQ(ModuleDirectory::open(moduleDir()).kind(), ModuleKind::Core);

    writeFile("manifest.json", R"({"name": "x", "version": "1.0.0"})");
    EXPECT_EQ(ModuleDirectory::open(moduleDir()).kind(), ModuleKind::Unknown);
}

TEST_F(UiPluginDirectoryTest, AQmlOnlyUiPluginExpectsNoPluginAtAll) {
    makeModuleDir();
    writeFile("manifest.json", kUiQmlQmlOnly);
    writeIn(QStringLiteral("qml/AccountsView.qml"), "Item {}");
    writeFile("accounts.png", "png");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::NotDeclared);
    EXPECT_EQ(dir.pluginExpectation(), PluginExpectation::NotExpected);
    EXPECT_EQ(dir.view().state, AssetResolution::Resolved);
    EXPECT_EQ(dir.icon().state, AssetResolution::Resolved);
}

TEST_F(UiPluginDirectoryTest, ACoreModuleWithNoMainIsStillBroken) {
    // Even carrying a `view`: the TYPE is what permits a package to have no
    // plugin, and a core module is its plugin.
    makeModuleDir();
    writeFile("manifest.json",
              R"({"name": "m", "version": "1.0.0", "type": "core",
                  "view": "qml/Main.qml"})");
    writeIn(QStringLiteral("qml/Main.qml"), "Item {}");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::NotDeclared);
    EXPECT_EQ(dir.view().state, AssetResolution::Resolved);
    EXPECT_EQ(dir.pluginExpectation(), PluginExpectation::Missing);
}

TEST_F(UiPluginDirectoryTest, AUiPluginThatDeclaresNoViewStillNeedsAPlugin) {
    makeModuleDir();
    writeFile("manifest.json", R"({"name": "u", "version": "1.0.0", "type": "ui_qml"})");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.view().state, AssetResolution::NotDeclared);
    EXPECT_EQ(dir.pluginExpectation(), PluginExpectation::Missing);
}

TEST_F(UiPluginDirectoryTest, AnEmptyMainObjectDeclaresNoMain) {
    makeModuleDir();
    writeFile("manifest.json",
              R"({"name": "u", "version": "1.0.0", "type": "ui_qml",
                  "view": "qml/Main.qml", "main": {}})");
    writeIn(QStringLiteral("qml/Main.qml"), "Item {}");

    EXPECT_EQ(ModuleDirectory::open(moduleDir()).pluginExpectation(),
              PluginExpectation::NotExpected);
}

TEST_F(UiPluginDirectoryTest, APartiallyPopulatedMainStillNeedsThisVariant) {
    makeModuleDir();
    writeFile("manifest.json",
              R"({"name": "u", "version": "1.0.0", "type": "ui_qml",
                  "view": "qml/Main.qml", "main": {"linux-amd64": "u.so"}})");
    writeIn(QStringLiteral("qml/Main.qml"), "Item {}");
    writeFile("variant", "darwin-arm64\n");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.main().state, MainResolution::NoVariantMatch);
    EXPECT_EQ(dir.pluginExpectation(), PluginExpectation::Missing);
}

TEST_F(UiPluginDirectoryTest, AViewTheDirectoryDoesNotHaveIsReported) {
    makeModuleDir();
    writeFile("manifest.json", kUiQmlQmlOnly);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.view().state, AssetResolution::FileMissing);
    EXPECT_EQ(dir.view().declaredPath, QStringLiteral("qml/AccountsView.qml"));
    EXPECT_TRUE(dir.view().path.isEmpty());
    EXPECT_EQ(dir.icon().state, AssetResolution::FileMissing);
}

TEST_F(UiPluginDirectoryTest, AViewPointingOutsideTheDirectoryIsRefused) {
    makeModuleDir();
    writeFile("manifest.json",
              R"({"name": "u", "version": "1.0.0", "type": "ui_qml",
                  "view": "../../etc/passwd", "icon": "../icon.png"})");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.view().state, AssetResolution::OutsideModule);
    EXPECT_EQ(dir.icon().state, AssetResolution::OutsideModule);
    EXPECT_TRUE(dir.view().path.isEmpty());
}

TEST_F(UiPluginDirectoryTest, ACoreModuleDeclaresNeitherViewNorIcon) {
    makeModuleDir();
    writeFile("manifest.json", kManifestWithMap);

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.view().state, AssetResolution::NotDeclared);
    EXPECT_EQ(dir.icon().state, AssetResolution::NotDeclared);
}

TEST_F(UiPluginDirectoryTest, AnEmptyIconStringDeclaresNothing) {
    // Every 0.3.0 core module on disk writes "icon": "". It declares no icon;
    // it does not declare one that is missing.
    makeModuleDir();
    writeFile("manifest.json",
              R"({"name": "m", "version": "1.0.0", "type": "core", "icon": ""})");

    EXPECT_EQ(ModuleDirectory::open(moduleDir()).icon().state,
              AssetResolution::NotDeclared);
}

TEST_F(UiPluginDirectoryTest, AQmlOnlyPluginHasNoNameToDisagreeWith) {
    makeModuleDir();
    writeFile("manifest.json", kUiQmlQmlOnly);
    writeIn(QStringLiteral("qml/AccountsView.qml"), "Item {}");

    ModuleDirectory dir = ModuleDirectory::open(moduleDir());

    EXPECT_EQ(dir.compareNames(), NameAgreement::NoPlugin);
    EXPECT_FALSE(dir.embeddedMetadata().has_value());
}

