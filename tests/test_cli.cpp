#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <array>
#include <memory>
#include <vector>

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
        
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            result.exitCode = -1;
            return result;
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        
        result.exitCode = pclose(pipe);
        // On Unix, the exit code is in the upper 8 bits
        result.exitCode = WEXITSTATUS(result.exitCode);
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

TEST_F(CLITest, UnknownCommand_ReturnsError) {
    auto result = runCommand("unknown_command");
    
    EXPECT_NE(result.exitCode, 0);
    EXPECT_NE(result.output.find("Error"), std::string::npos);
    EXPECT_NE(result.output.find("Unknown command"), std::string::npos);
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
