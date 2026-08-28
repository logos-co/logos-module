#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

#include "test_examples_path.h"

// popen/pclose are spelled with a leading underscore in the Windows CRT, and
// _pclose returns the child's exit code directly rather than the encoded wait
// status, so WEXITSTATUS (which lives in <sys/wait.h>, absent on mingw) neither
// exists nor applies there.
#ifdef _WIN32
#define LOGOS_POPEN _popen
#define LOGOS_PCLOSE _pclose
#else
#include <sys/wait.h>
#define LOGOS_POPEN popen
#define LOGOS_PCLOSE pclose
#endif

// =============================================================================
// CLI Test Fixture
// =============================================================================

class CLITest : public ::testing::Test {
protected:
    std::string lmBinary;
    
    void SetUp() override {
        // Check for LM_BINARY environment variable first
        const char* envBinary = std::getenv("LM_BINARY");
        if (envBinary && std::string(envBinary).length() > 0) {
            lmBinary = envBinary;
        } else {
            // Try to find the binary in common build locations
            // TODO: this is dumb, fix
            std::vector<std::string> possiblePaths = {
                // CMake build paths
                "./lm",
                "./bin/lm",
                "../bin/lm",
                "../../bin/lm",
                // Nix result paths (when running from workspace root)
                "./result/bin/lm",
                "../result/bin/lm",
                "../../result/bin/lm",
                // Relative to test executable location
                "../lm",
            };
            
            for (const auto& path : possiblePaths) {
                if (std::system(("test -x " + path + " 2>/dev/null").c_str()) == 0) {
                    lmBinary = path;
                    break;
                }
            }
        }
        
        // Skip tests if binary not found
        if (lmBinary.empty()) {
            GTEST_SKIP() << "lm binary not found. Set LM_BINARY environment variable or run from workspace root with ./result/bin/.";
        }
    }
    
    // Execute command and capture output
    struct CommandResult {
        int exitCode;
        std::string output;
    };
    
    CommandResult runCommand(const std::string& args) {
        CommandResult result;
        std::string command = lmBinary + " " + args + " 2>&1";
        
        std::array<char, 4096> buffer;
        std::string output;
        
        FILE* pipe = LOGOS_POPEN(command.c_str(), "r");
        if (!pipe) {
            result.exitCode = -1;
            return result;
        }

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }

        const int status = LOGOS_PCLOSE(pipe);
#ifdef _WIN32
        result.exitCode = status;
#else
        // On Unix, the exit code is in the upper 8 bits
        result.exitCode = WEXITSTATUS(status);
#endif
        result.output = output;
        
        return result;
    }
};

// =============================================================================
// CLI Test Fixture with Real Plugin
// =============================================================================

class CLIPluginTest : public CLITest {
protected:
    std::string testPlugin;
    
    void SetUp() override {
        // First call parent SetUp to find lm binary
        CLITest::SetUp();
        
        // Determine platform-specific plugin extension
#ifdef __APPLE__
        std::string pluginName = "package_manager_plugin.dylib";
#else
        std::string pluginName = "package_manager_plugin.so";
#endif
        
        // Check for TEST_PLUGIN environment variable first
        const char* envPlugin = std::getenv("TEST_PLUGIN");
        if (envPlugin && std::string(envPlugin).length() > 0) {
            testPlugin = envPlugin;
        } else {
            // Try to find the test plugin in common locations
            std::vector<std::string> possiblePaths = {
                // From workspace root
                "tests/examples/" + pluginName,
                "./tests/examples/" + pluginName,
                // From build directory
                "../tests/examples/" + pluginName,
                "../../tests/examples/" + pluginName,
                "../../../tests/examples/" + pluginName,
                // From Nix build (source is copied)
                "examples/" + pluginName,
            };
            
            for (const auto& path : possiblePaths) {
                if (std::system(("test -f " + path + " 2>/dev/null").c_str()) == 0) {
                    testPlugin = path;
                    break;
                }
            }
        }
        
        // Skip tests if plugin not found
        if (testPlugin.empty()) {
            GTEST_SKIP() << "Test plugin not found. Set TEST_PLUGIN environment variable or run from workspace root.";
        }
    }
};

// =============================================================================
// Version and Help Tests
// =============================================================================

TEST_F(CLITest, Version_ShowsVersionInfo) {
    auto result = runCommand("--version");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("lm"), std::string::npos);
    EXPECT_NE(result.output.find("version"), std::string::npos);
    EXPECT_NE(result.output.find("0.1.0"), std::string::npos);
}

TEST_F(CLITest, VersionShort_ShowsVersionInfo) {
    auto result = runCommand("-v");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("version"), std::string::npos);
}

TEST_F(CLITest, Help_ShowsUsageInfo) {
    auto result = runCommand("--help");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Usage:"), std::string::npos);
    EXPECT_NE(result.output.find("metadata"), std::string::npos);
    EXPECT_NE(result.output.find("methods"), std::string::npos);
}

TEST_F(CLITest, HelpShort_ShowsUsageInfo) {
    auto result = runCommand("-h");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Usage:"), std::string::npos);
}

TEST_F(CLITest, NoArgs_ShowsUsageInfo) {
    auto result = runCommand("");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Usage:"), std::string::npos);
}

// =============================================================================
// Command Help Tests
// =============================================================================

TEST_F(CLITest, MetadataHelp_ShowsCommandHelp) {
    auto result = runCommand("metadata --help");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("metadata"), std::string::npos);
    EXPECT_NE(result.output.find("--json"), std::string::npos);
}

TEST_F(CLITest, MethodsHelp_ShowsCommandHelp) {
    auto result = runCommand("methods --help");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("methods"), std::string::npos);
    EXPECT_NE(result.output.find("--json"), std::string::npos);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(CLITest, DefaultMode_NonExistentPath_ReturnsError) {
    auto result = runCommand("unknown_command");
    
    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Error"), std::string::npos);
    EXPECT_NE(result.output.find("not found"), std::string::npos);
}

TEST_F(CLITest, MetadataMissingPath_ReturnsError) {
    auto result = runCommand("metadata");
    
    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Error"), std::string::npos);
    EXPECT_NE(result.output.find("Missing plugin path"), std::string::npos);
}

TEST_F(CLITest, MethodsMissingPath_ReturnsError) {
    auto result = runCommand("methods");
    
    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Error"), std::string::npos);
    EXPECT_NE(result.output.find("Missing plugin path"), std::string::npos);
}

TEST_F(CLITest, NonExistentPlugin_ReturnsError) {
    auto result = runCommand("metadata /nonexistent/path/plugin.so");
    
    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Error"), std::string::npos);
    EXPECT_NE(result.output.find("not found"), std::string::npos);
}

TEST_F(CLITest, UnknownOption_ReturnsError) {
    auto result = runCommand("metadata --unknown-option plugin.so");
    
    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Error"), std::string::npos);
    EXPECT_NE(result.output.find("Unknown option"), std::string::npos);
}

// =============================================================================
// Real Plugin Tests - Metadata Command
// =============================================================================

TEST_F(CLIPluginTest, Metadata_ShowsCorrectName) {
    auto result = runCommand("metadata " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Name should be exactly "package_manager"
    EXPECT_NE(result.output.find("Name:         package_manager"), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_ShowsCorrectVersion) {
    auto result = runCommand("metadata " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Version should be exactly "1.0.0"
    EXPECT_NE(result.output.find("Version:      1.0.0"), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_ShowsCorrectDescription) {
    auto result = runCommand("metadata " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Description should be "Plugin manager"
    EXPECT_NE(result.output.find("Description:  Plugin manager"), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_ShowsCorrectAuthor) {
    auto result = runCommand("metadata " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Author should be "Logos Core Team"
    EXPECT_NE(result.output.find("Author:       Logos Core Team"), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_ShowsCorrectType) {
    auto result = runCommand("metadata " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Type should be "core"
    EXPECT_NE(result.output.find("Type:         core"), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_ShowsNoDependencies) {
    auto result = runCommand("metadata " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Dependencies should be "(none)"
    EXPECT_NE(result.output.find("Dependencies: (none)"), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_JsonHasCorrectName) {
    auto result = runCommand("metadata " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("\"name\": \"package_manager\""), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_JsonHasCorrectVersion) {
    auto result = runCommand("metadata " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("\"version\": \"1.0.0\""), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_JsonHasCorrectAuthor) {
    auto result = runCommand("metadata " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("\"author\": \"Logos Core Team\""), std::string::npos);
}

TEST_F(CLIPluginTest, Metadata_JsonHasCorrectType) {
    auto result = runCommand("metadata " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("\"type\": \"core\""), std::string::npos);
}

// =============================================================================
// Real Plugin Tests - Methods Command
// =============================================================================

TEST_F(CLIPluginTest, Methods_ShowsInstallPluginMethod) {
    auto result = runCommand("methods " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Check full method signature
    EXPECT_NE(result.output.find("bool installPlugin(QString pluginPath)"), std::string::npos);
    EXPECT_NE(result.output.find("Signature: installPlugin(QString)"), std::string::npos);
    EXPECT_NE(result.output.find("Invokable: yes"), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_ShowsGetPackagesMethod) {
    auto result = runCommand("methods " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Check full method signature
    EXPECT_NE(result.output.find("QJsonArray getPackages()"), std::string::npos);
    EXPECT_NE(result.output.find("Signature: getPackages()"), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_ShowsInitLogosMethod) {
    auto result = runCommand("methods " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Check full method signature
    EXPECT_NE(result.output.find("void initLogos(LogosAPI* logosAPIInstance)"), std::string::npos);
    EXPECT_NE(result.output.find("Signature: initLogos(LogosAPI*)"), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_ShowsTestPluginCallMethod) {
    auto result = runCommand("methods " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // Check full method signature
    EXPECT_NE(result.output.find("QString testPluginCall(QString foo)"), std::string::npos);
    EXPECT_NE(result.output.find("Signature: testPluginCall(QString)"), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_HasExactlyFourMethods) {
    auto result = runCommand("methods " + testPlugin);
    
    EXPECT_EQ(result.exitCode, 0);
    // All four methods should be present
    EXPECT_NE(result.output.find("installPlugin"), std::string::npos);
    EXPECT_NE(result.output.find("getPackages"), std::string::npos);
    EXPECT_NE(result.output.find("initLogos"), std::string::npos);
    EXPECT_NE(result.output.find("testPluginCall"), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_JsonHasInstallPluginMethod) {
    auto result = runCommand("methods " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("\"name\": \"installPlugin\""), std::string::npos);
    EXPECT_NE(result.output.find("\"signature\": \"installPlugin(QString)\""), std::string::npos);
    EXPECT_NE(result.output.find("\"returnType\": \"bool\""), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_JsonHasGetPackagesMethod) {
    auto result = runCommand("methods " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("\"name\": \"getPackages\""), std::string::npos);
    EXPECT_NE(result.output.find("\"returnType\": \"QJsonArray\""), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_JsonHasParameters) {
    auto result = runCommand("methods " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    // installPlugin has a parameter named "pluginPath" of type "QString"
    EXPECT_NE(result.output.find("\"name\": \"pluginPath\""), std::string::npos);
    EXPECT_NE(result.output.find("\"type\": \"QString\""), std::string::npos);
}

TEST_F(CLIPluginTest, Methods_JsonAllMethodsInvokable) {
    auto result = runCommand("methods " + testPlugin + " --json");
    
    EXPECT_EQ(result.exitCode, 0);
    // All methods should be invokable
    EXPECT_NE(result.output.find("\"isInvokable\": true"), std::string::npos);
}

// =============================================================================
// Module Directory Tests
//
// `lm` takes an installed module directory wherever it takes a plugin file, and
// resolves the plugin out of manifest.json. The failure modes carry most of the
// weight here: one "not found" for all of them tells whoever hits it nothing.
// =============================================================================

namespace fs = std::filesystem;

class CLIDirectoryTest : public CLITest {
protected:
    fs::path dir;
    std::string examplePlugin;

    void SetUp() override {
        CLITest::SetUp();

        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir = fs::temp_directory_path()
              / (std::string("lm_dir_") + info->test_suite_name() + "_" + info->name());
        std::error_code ec;
        fs::remove_all(dir, ec);
        ASSERT_TRUE(fs::create_directories(dir, ec)) << ec.message();

        examplePlugin = findExamplePlugin();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Shares CLIPluginTest's TEST_PLUGIN convention.
    static std::string findExamplePlugin() {
        const char* env = std::getenv("TEST_PLUGIN");
        if (env && std::string(env).length() > 0) {
            return env;
        }
#ifdef __APPLE__
        const std::string name = "package_manager_plugin.dylib";
#else
        const std::string name = "package_manager_plugin.so";
#endif
        for (const std::string& prefix : {"tests/examples/", "../tests/examples/",
                                          "../../tests/examples/", "examples/"}) {
            if (fs::exists(prefix + name)) return prefix + name;
        }
        return std::string();
    }

    fs::path sub(const std::string& name) {
        const fs::path path = dir / name;
        std::error_code ec;
        fs::create_directories(path, ec);
        return path;
    }

    void writeIn(const fs::path& where, const std::string& name, const std::string& content) {
        std::ofstream file(where / name, std::ios::binary);
        ASSERT_TRUE(file.is_open()) << (where / name).string();
        file << content;
    }

    // A hard failure rather than a skip: the plugin is checked in, and a skip
    // would render as a pass.
    void copyPluginInto(const fs::path& where, const std::string& name) {
        ASSERT_FALSE(examplePlugin.empty()) << "example plugin not found; set TEST_PLUGIN";
        std::error_code ec;
        fs::copy_file(examplePlugin, where / name, fs::copy_options::overwrite_existing, ec);
        ASSERT_FALSE(ec) << ec.message();
    }

    static std::string quote(const fs::path& path) { return "\"" + path.string() + "\""; }

    // A directory whose manifest names the example plugin under one variant.
    fs::path goodDirectory(const std::string& name = "good") {
        const fs::path where = sub(name);
        copyPluginInto(where, "the_plugin");
        writeIn(where, "manifest.json",
                R"({"name":"package_manager","main":{"darwin-arm64":"the_plugin"}})");
        writeIn(where, "variant", "darwin-arm64");
        return where;
    }
};

TEST_F(CLIDirectoryTest, NoManifest_NamesTheMissingManifest) {
    copyPluginInto(dir, "the_plugin");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("no manifest.json"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, MalformedManifest_SaysItIsNotValidJson) {
    writeIn(dir, "manifest.json", "{\"name\": \"package_manager\", \"main\":\n");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("not valid JSON"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, NoMain_SaysNothingIsNamedAsThePlugin) {
    copyPluginInto(dir, "the_plugin");
    writeIn(dir, "manifest.json", R"({"name":"package_manager","version":"1.0.0"})");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("declares no \"main\""), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, MainNamesAMissingFile_SaysWhichFile) {
    writeIn(dir, "manifest.json",
            R"({"name":"package_manager","main":{"darwin-arm64":"the_plugin"}})");
    writeIn(dir, "variant", "darwin-arm64");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("names a main that is not in the module directory"),
              std::string::npos) << result.output;
    EXPECT_NE(result.output.find("the_plugin"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, NoVariantMatch_ShowsWhatIsOfferedAndWhatWasTried) {
    copyPluginInto(dir, "the_plugin");
    writeIn(dir, "manifest.json",
            R"({"name":"package_manager","main":{"darwin-arm64":"the_plugin"}})");
    writeIn(dir, "variant", "darwin-x86_64");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("declares no main for this variant"), std::string::npos)
        << result.output;
    // Both sides of the mismatch, or the reader cannot tell which is wrong.
    EXPECT_NE(result.output.find("main has: darwin-arm64"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("tried:    darwin-x86_64"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, NoVariantFileAtAll_PointsAtTheVariantFlag) {
    copyPluginInto(dir, "the_plugin");
    writeIn(dir, "manifest.json",
            R"({"name":"package_manager","main":{"darwin-arm64":"the_plugin"}})");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("--variant"), std::string::npos) << result.output;
}

// The whole point of the failure reporting: four different repairs must not
// arrive as one message.
TEST_F(CLIDirectoryTest, TheFourMainFailuresAreFourDifferentMessages) {
    const fs::path noManifest = sub("no_manifest");
    copyPluginInto(noManifest, "the_plugin");

    const fs::path noMain = sub("no_main");
    copyPluginInto(noMain, "the_plugin");
    writeIn(noMain, "manifest.json", R"({"name":"package_manager"})");

    const fs::path missingFile = sub("missing_file");
    writeIn(missingFile, "manifest.json",
            R"({"name":"package_manager","main":{"darwin-arm64":"the_plugin"}})");
    writeIn(missingFile, "variant", "darwin-arm64");

    const fs::path wrongVariant = sub("wrong_variant");
    copyPluginInto(wrongVariant, "the_plugin");
    writeIn(wrongVariant, "manifest.json",
            R"({"name":"package_manager","main":{"darwin-arm64":"the_plugin"}})");
    writeIn(wrongVariant, "variant", "linux-amd64");

    std::set<std::string> messages;
    for (const fs::path& each : {noManifest, noMain, missingFile, wrongVariant}) {
        auto result = runCommand("metadata " + quote(each));
        EXPECT_NE(result.exitCode, 0) << each.string();

        // Blank the path out first. It differs per case anyway, so leaving it in
        // would let four identical complaints pass as four distinct ones.
        std::string first = result.output.substr(0, result.output.find('\n'));
        for (size_t at = first.find(each.string()); at != std::string::npos;
             at = first.find(each.string())) {
            first.replace(at, each.string().size(), "<dir>");
        }
        messages.insert(first);
    }
    EXPECT_EQ(messages.size(), 4u) << "four different repairs must not arrive as one message";
}

TEST_F(CLIDirectoryTest, Metadata_ResolvesTheMainFromAVariantMap) {
    const fs::path where = goodDirectory();

    auto result = runCommand("metadata " + quote(where));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("Name:         package_manager"), std::string::npos)
        << result.output;
    EXPECT_NE(result.output.find("Main:         the_plugin  [main.darwin-arm64]"),
              std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, Metadata_ResolvesThePlainStringMainForm) {
    copyPluginInto(dir, "the_plugin");
    writeIn(dir, "manifest.json", R"({"name":"package_manager","main":"the_plugin"})");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("Name:         package_manager"), std::string::npos)
        << result.output;
}

TEST_F(CLIDirectoryTest, Metadata_UnsignedDirectoryIsReportedAsUnsigned) {
    const fs::path where = goodDirectory();

    auto result = runCommand("metadata " + quote(where));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("package is unsigned"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, Metadata_PresentSignatureIsReportedWithItsSize) {
    const fs::path where = goodDirectory();
    writeIn(where, "manifest.sig", "0123456789");

    auto result = runCommand("metadata " + quote(where));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("manifest.sig (10 bytes)"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, Metadata_WarnsWhenTheManifestNameDisagreesWithThePlugin) {
    copyPluginInto(dir, "the_plugin");
    writeIn(dir, "manifest.json", R"({"name":"innocuous_module","main":"the_plugin"})");

    auto result = runCommand("metadata " + quote(dir));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("WARNING"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("innocuous_module"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("package_manager"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, Metadata_AgreeingNamesProduceNoWarning) {
    const fs::path where = goodDirectory();

    auto result = runCommand("metadata " + quote(where));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_EQ(result.output.find("WARNING"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, Json_MetadataObjectGainsModuleDirectory) {
    const fs::path where = goodDirectory();
    writeIn(where, "manifest.sig", "0123456789");

    auto result = runCommand("metadata " + quote(where) + " --json");

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("\"module_directory\""), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("\"installed_variant\": \"darwin-arm64\""), std::string::npos)
        << result.output;
    EXPECT_NE(result.output.find("\"state\": \"resolved\""), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("\"present\": true"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("\"name_agreement\": \"agree\""), std::string::npos)
        << result.output;
    // The metadata keys other tools already read are untouched.
    EXPECT_NE(result.output.find("\"name\": \"package_manager\""), std::string::npos)
        << result.output;
}

// Back-compat: a plugin file genuinely has no manifest, so the key is absent
// rather than empty — a reader cannot mistake one for the other, and no
// directory report is fabricated around the file's parent directory either.
TEST_F(CLIPluginTest, MetadataJson_PluginFileHasNoModuleDirectoryKey) {
    auto result = runCommand("metadata " + testPlugin + " --json");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.output.find("module_directory"), std::string::npos) << result.output;
}

TEST_F(CLIPluginTest, DefaultJson_PluginFileHasNoModuleDirectoryKey) {
    auto result = runCommand(testPlugin + " --json");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.output.find("module_directory"), std::string::npos) << result.output;
}

TEST_F(CLIPluginTest, DefaultHuman_PluginFileHasNoDirectoryReport) {
    auto result = runCommand(testPlugin);

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.output.find("Module Directory:"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, Variant_FlagOverridesTheInstalledVariant) {
    const fs::path where = sub("skewed");
    copyPluginInto(where, "the_plugin");
    writeIn(where, "manifest.json",
            R"({"name":"package_manager","main":{"darwin-arm64":"the_plugin"}})");
    // The live bundler/lgpm spelling skew: the file says one thing, the
    // manifest keys another, and the caller supplies the reconciliation.
    writeIn(where, "variant", "darwin-x86_64");

    auto result = runCommand("metadata " + quote(where) + " --variant darwin-arm64");

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("Main:         the_plugin  [main.darwin-arm64]"),
              std::string::npos) << result.output;
    // The installed variant is still reported as what it is, not as the override.
    EXPECT_NE(result.output.find("Variant:      darwin-x86_64"), std::string::npos)
        << result.output;
}

TEST_F(CLIDirectoryTest, Variant_FlagIsRefusedOnAPluginFile) {
    copyPluginInto(dir, "the_plugin");

    auto result = runCommand("metadata " + quote(dir / "the_plugin") + " --variant darwin-arm64");

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("is a plugin file"), std::string::npos) << result.output;
}

TEST_F(CLIDirectoryTest, Variant_FlagNeedsAName) {
    auto result = runCommand("metadata " + quote(dir) + " --variant");

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("--variant needs a variant name"), std::string::npos)
        << result.output;
}

TEST_F(CLITest, Help_MentionsDirectoriesAndTheVariantFlag) {
    auto result = runCommand("--help");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("module directory"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("--variant"), std::string::npos) << result.output;
}

// -----------------------------------------------------------------------------
// Plugin-file back-compat. Other tools parse lm's output, so a path must come
// back spelled the way it was typed. Resolving a directory to its main is a new
// chance to absolutise the plugin-file path too, and nothing else notices.
// -----------------------------------------------------------------------------

class CLIPathEchoTest : public CLIDirectoryTest {
protected:
    void SetUp() override {
        CLIDirectoryTest::SetUp();
        if (lmBinary.empty()) return;
        // Run lm from inside the scratch dir so the argument can be a bare
        // relative name; an absolutised echo is then unmistakable.
        lmBinary = "cd " + quote(dir) + " && " + fs::absolute(lmBinary).string();
    }

    // Exists, but is not a loadable plugin — the only way to reach the
    // "Failed to extract metadata" path on a file the user really named.
    void writeNonPlugin() { writeIn(dir, "notaplugin.txt", "not a plugin, just text\n"); }
};

TEST_F(CLIPathEchoTest, Metadata_EchoesTheRelativePathAsTyped) {
    writeNonPlugin();

    auto result = runCommand("metadata notaplugin.txt");

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Failed to extract metadata from: notaplugin.txt\n"),
              std::string::npos)
        << result.output;
}

TEST_F(CLIPathEchoTest, DefaultJson_EchoesTheRelativePathAsTyped) {
    writeNonPlugin();

    auto result = runCommand("notaplugin.txt --json");

    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Failed to extract metadata from: notaplugin.txt\n"),
              std::string::npos)
        << result.output;
}

// -----------------------------------------------------------------------------
// Directory tests that need the plugin to actually load. Named to match the
// nix check's `--exclude-regex CLIPluginTest`: the checked-in example plugin
// hardcodes /nix/store paths that are not in the sandbox's closure.
// -----------------------------------------------------------------------------

class CLIPluginTestDirectory : public CLIDirectoryTest {};

TEST_F(CLIPluginTestDirectory, Methods_ResolvesTheMainOutOfTheDirectory) {
    const fs::path where = goodDirectory();

    auto result = runCommand("methods " + quote(where));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("installPlugin"), std::string::npos) << result.output;
}

// The JSON shape guarantee: `methods`/`events` print a bare array, and turning
// that into an object to carry the directory report is exactly the break other
// parsers would feel. They stay an array; `lm <dir> --json` carries the report.
TEST_F(CLIPluginTestDirectory, MethodsJson_StaysABareArray) {
    const fs::path where = goodDirectory();

    auto result = runCommand("methods " + quote(where) + " --json");

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_EQ(result.output.find_first_not_of(" \t\r\n"), result.output.find('['))
        << result.output;
    EXPECT_EQ(result.output.find("module_directory"), std::string::npos) << result.output;
}

TEST_F(CLIPluginTestDirectory, DefaultJson_KeepsItsSectionsAndAddsModuleDirectory) {
    const fs::path where = goodDirectory();

    auto result = runCommand(quote(where) + " --json");

    EXPECT_EQ(result.exitCode, 0) << result.output;
    for (const char* key : {"\"metadata\"", "\"methods\"", "\"events\"", "\"module_directory\""}) {
        EXPECT_NE(result.output.find(key), std::string::npos) << key << "\n" << result.output;
    }
}

TEST_F(CLIPluginTestDirectory, Default_HumanPrintsTheDirectoryReportExactlyOnce) {
    const fs::path where = goodDirectory();

    auto result = runCommand(quote(where));

    EXPECT_EQ(result.exitCode, 0) << result.output;
    size_t count = 0;
    for (size_t at = result.output.find("Module Directory:"); at != std::string::npos;
         at = result.output.find("Module Directory:", at + 1)) {
        ++count;
    }
    EXPECT_EQ(count, 1u) << result.output;
}

// =============================================================================
// `lm verify`: the library reports, the CLI decides the exit status
// =============================================================================

class CLIVerifyTest : public CLITest {
protected:
    std::string examples() const { return testExamplesDir(); }
    std::string signedInstall() const { return examples() + "/installed_signed"; }
    std::string qmlOnlyInstall() const { return examples() + "/installed_qml_only"; }

    // The DID whose key signed installed_signed/manifest.json, and an
    // unrelated but equally well-formed one.
    static constexpr const char* kSignerDid =
        "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6IkxQMjNNbFI5eWQ4d2VUTk9B"
        "OElEeVNUZ2REdm41cGIxaTMteE5YTm9fWjgifQ";
    static constexpr const char* kOtherDid =
        "did:jwk:eyJjcnYiOiJFZDI1NTE5Iiwia3R5IjoiT0tQIiwieCI6ImpBazdZc3BXUnk1ckNQcFct"
        "SnItRGlRWHNGOUhJaDhKUVlIVGVZWE9CZ2MifQ";
};

TEST_F(CLIVerifyTest, AnUntouchedInstallPassesAndExitsZero) {
    auto result = runCommand("verify " + signedInstall());

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("RESULT: pass"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("Integrity:  ok"), std::string::npos) << result.output;
}

TEST_F(CLIVerifyTest, AUiPluginGoesThroughTheSameCommand) {
    auto result = runCommand("verify " + qmlOnlyInstall());

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("RESULT: pass"), std::string::npos) << result.output;
}

TEST_F(CLIVerifyTest, ThePinnedDidIsReportedAsChecked) {
    auto result = runCommand("verify " + signedInstall() + " --did " + kSignerDid);

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("Signature:  ok"), std::string::npos) << result.output;
}

TEST_F(CLIVerifyTest, AWrongDidFailsTheRunEvenThoughEveryOtherCheckPasses) {
    auto result = runCommand("verify " + signedInstall() + " --did " + kOtherDid);

    EXPECT_EQ(result.exitCode, 1) << result.output;
    EXPECT_NE(result.output.find("Signature:  MISMATCH"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("Integrity:  ok"), std::string::npos) << result.output;
}

TEST_F(CLIVerifyTest, AnUnparseableDidIsReportedAsTheCallersError) {
    auto result = runCommand("verify " + signedInstall() + " --did did:web:example.com");

    EXPECT_EQ(result.exitCode, 1) << result.output;
    EXPECT_NE(result.output.find("bad --did"), std::string::npos) << result.output;
}

TEST_F(CLIVerifyTest, WithoutADidNoSignatureQuestionIsAnswered) {
    auto result = runCommand("verify " + signedInstall());

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("Signature:  not checked"), std::string::npos)
        << result.output;
}

TEST_F(CLIVerifyTest, ADirectoryThatCannotBeCheckedExitsNonZero) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lm_verify_empty_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    auto result = runCommand("verify " + dir.string());

    EXPECT_EQ(result.exitCode, 1) << result.output;
    EXPECT_NE(result.output.find("no manifest.json"), std::string::npos) << result.output;
    std::filesystem::remove_all(dir);
}

TEST_F(CLIVerifyTest, VerifyRefusesABarePluginFile) {
    auto result = runCommand("verify " + signedInstall() + "/signed_module_plugin.dylib");

    EXPECT_EQ(result.exitCode, 1) << result.output;
    EXPECT_NE(result.output.find("installed module directory"), std::string::npos)
        << result.output;
}

TEST_F(CLIVerifyTest, DidOnlyMeansSomethingToVerify) {
    auto result = runCommand("metadata " + signedInstall() + " --did " + kSignerDid);

    EXPECT_EQ(result.exitCode, 1) << result.output;
    EXPECT_NE(result.output.find("--did"), std::string::npos) << result.output;
}

TEST_F(CLIVerifyTest, TheJsonReportNamesEveryVerdict) {
    auto result = runCommand("verify " + signedInstall() + " --json --did " + kSignerDid);

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("\"integrity\": \"ok\""), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("\"state\": \"ok\""), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("\"claimed_did\""), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("\"valid\": true"), std::string::npos) << result.output;
}

// =============================================================================
// A ui_qml package with no plugin is a complete package
// =============================================================================

class CLIUiPluginTest : public CLITest {
protected:
    std::string qmlOnlyInstall() const {
        return testExamplesDir() + "/installed_qml_only";
    }
};

TEST_F(CLIUiPluginTest, InspectingAQmlOnlyUiPluginSucceeds) {
    auto result = runCommand(qmlOnlyInstall());

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("Type:         ui_qml  (UI plugin)"), std::string::npos)
        << result.output;
    EXPECT_NE(result.output.find("View:         qml/Main.qml"), std::string::npos)
        << result.output;
    EXPECT_NE(result.output.find("Icon:         assets/icon.png"), std::string::npos)
        << result.output;
    EXPECT_NE(result.output.find("(no plugin"), std::string::npos) << result.output;
}

TEST_F(CLIUiPluginTest, MethodsOfAQmlOnlyUiPluginAreAnEmptyList) {
    auto result = runCommand("methods " + qmlOnlyInstall() + " --json");

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("["), std::string::npos) << result.output;
}

TEST_F(CLIUiPluginTest, TheDirectoryJsonCarriesTheUiFields) {
    auto result = runCommand(qmlOnlyInstall() + " --json");

    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("\"plugin\": \"not_expected\""), std::string::npos)
        << result.output;
    EXPECT_NE(result.output.find("\"kind\": \"ui_plugin\""), std::string::npos)
        << result.output;
    EXPECT_NE(result.output.find("\"name_agreement\": \"no_plugin\""), std::string::npos)
        << result.output;
}
