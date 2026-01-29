#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <array>
#include <memory>

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
