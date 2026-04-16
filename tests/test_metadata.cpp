#include <gtest/gtest.h>
#include "module_metadata.h"
#include "logos_module.h"
#include <QJsonArray>
#include <QString>
#include <string>
#include <vector>

using namespace ModuleLib;

// =============================================================================
// fromCustomMetadata Tests
// =============================================================================

TEST(MetadataTest, FromCustomMetadata_ValidData) {
    QJsonObject json;
    json["name"] = "test_plugin";
    json["version"] = "1.0.0";
    json["description"] = "A test plugin";
    json["author"] = "Test Author";
    json["type"] = "core";
    
    auto metadata = ModuleMetadata::fromCustomMetadata(json);
    
    EXPECT_TRUE(metadata.isValid());
    EXPECT_EQ(metadata.name.toStdString(), "test_plugin");
    EXPECT_EQ(metadata.version.toStdString(), "1.0.0");
    EXPECT_EQ(metadata.description.toStdString(), "A test plugin");
    EXPECT_EQ(metadata.author.toStdString(), "Test Author");
    EXPECT_EQ(metadata.type.toStdString(), "core");
}

TEST(MetadataTest, FromCustomMetadata_MinimalData) {
    QJsonObject json;
    json["name"] = "minimal_plugin";
    
    auto metadata = ModuleMetadata::fromCustomMetadata(json);
    
    EXPECT_TRUE(metadata.isValid());
    EXPECT_EQ(metadata.name.toStdString(), "minimal_plugin");
    EXPECT_TRUE(metadata.version.isEmpty());
    EXPECT_TRUE(metadata.description.isEmpty());
}

TEST(MetadataTest, FromCustomMetadata_EmptyName) {
    QJsonObject json;
    json["version"] = "1.0.0";
    // No name provided
    
    auto metadata = ModuleMetadata::fromCustomMetadata(json);
    
    EXPECT_FALSE(metadata.isValid());
}

TEST(MetadataTest, FromCustomMetadata_EmptyObject) {
    QJsonObject json;
    
    auto metadata = ModuleMetadata::fromCustomMetadata(json);
    
    EXPECT_FALSE(metadata.isValid());
}

TEST(MetadataTest, FromCustomMetadata_WithDependencies) {
    QJsonObject json;
    json["name"] = "dependent_plugin";
    
    QJsonArray deps;
    deps.append("dep1");
    deps.append("dep2");
    deps.append("dep3");
    json["dependencies"] = deps;
    
    auto metadata = ModuleMetadata::fromCustomMetadata(json);
    
    EXPECT_TRUE(metadata.isValid());
    EXPECT_EQ(metadata.dependencies.size(), 3);
    EXPECT_EQ(metadata.dependencies[0].toStdString(), "dep1");
    EXPECT_EQ(metadata.dependencies[1].toStdString(), "dep2");
    EXPECT_EQ(metadata.dependencies[2].toStdString(), "dep3");
}

TEST(MetadataTest, FromCustomMetadata_EmptyDependencies) {
    QJsonObject json;
    json["name"] = "no_deps_plugin";
    json["dependencies"] = QJsonArray();
    
    auto metadata = ModuleMetadata::fromCustomMetadata(json);
    
    EXPECT_TRUE(metadata.isValid());
    EXPECT_TRUE(metadata.dependencies.isEmpty());
}

TEST(MetadataTest, FromCustomMetadata_RawMetadataPreserved) {
    QJsonObject json;
    json["name"] = "test_plugin";
    json["customField"] = "customValue";
    json["anotherField"] = 42;
    
    auto metadata = ModuleMetadata::fromCustomMetadata(json);
    
    EXPECT_TRUE(metadata.isValid());
    EXPECT_EQ(metadata.rawMetadata.value("customField").toString().toStdString(), "customValue");
    EXPECT_EQ(metadata.rawMetadata.value("anotherField").toInt(), 42);
}

// =============================================================================
// fromJson Tests (Qt plugin metadata format)
// =============================================================================

TEST(MetadataTest, FromJson_ValidPluginMetadata) {
    QJsonObject customMetadata;
    customMetadata["name"] = "qt_plugin";
    customMetadata["version"] = "2.0.0";
    customMetadata["description"] = "A Qt plugin";
    
    QJsonObject pluginMetadata;
    pluginMetadata["MetaData"] = customMetadata;
    
    auto result = ModuleMetadata::fromJson(pluginMetadata);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name.toStdString(), "qt_plugin");
    EXPECT_EQ(result->version.toStdString(), "2.0.0");
}

TEST(MetadataTest, FromJson_MissingMetaDataSection) {
    QJsonObject pluginMetadata;
    pluginMetadata["name"] = "invalid";
    // No MetaData section
    
    auto result = ModuleMetadata::fromJson(pluginMetadata);
    
    EXPECT_FALSE(result.has_value());
}

TEST(MetadataTest, FromJson_EmptyMetaDataSection) {
    QJsonObject pluginMetadata;
    pluginMetadata["MetaData"] = QJsonObject();
    
    auto result = ModuleMetadata::fromJson(pluginMetadata);
    
    EXPECT_FALSE(result.has_value());
}

TEST(MetadataTest, FromJson_InvalidNameInMetaData) {
    QJsonObject customMetadata;
    // No name, which makes it invalid
    customMetadata["version"] = "1.0.0";
    
    QJsonObject pluginMetadata;
    pluginMetadata["MetaData"] = customMetadata;
    
    auto result = ModuleMetadata::fromJson(pluginMetadata);
    
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// isValid Tests
// =============================================================================

TEST(MetadataTest, IsValid_WithName) {
    ModuleMetadata metadata;
    metadata.name = "valid_plugin";
    
    EXPECT_TRUE(metadata.isValid());
}

TEST(MetadataTest, IsValid_EmptyName) {
    ModuleMetadata metadata;
    metadata.name = "";
    
    EXPECT_FALSE(metadata.isValid());
}

TEST(MetadataTest, IsValid_DefaultConstructed) {
    ModuleMetadata metadata;
    
    EXPECT_FALSE(metadata.isValid());
}

// =============================================================================
// getModuleName Tests
// =============================================================================

TEST(GetModuleNameTest, NonExistentPath_ReturnsEmpty) {
    std::string name = LogosModule::getModuleName("/nonexistent/path/plugin.so");
    EXPECT_TRUE(name.empty());
}

TEST(GetModuleNameTest, EmptyPath_ReturnsEmpty) {
    std::string name = LogosModule::getModuleName("");
    EXPECT_TRUE(name.empty());
}

TEST(GetModuleNameTest, InvalidFile_ReturnsEmpty) {
    std::string name = LogosModule::getModuleName("/dev/null");
    EXPECT_TRUE(name.empty());
}

// =============================================================================
// getModuleDependencies Tests
// =============================================================================

TEST(GetModuleDependenciesTest, NonExistentPath_ReturnsEmpty) {
    std::vector<std::string> deps = LogosModule::getModuleDependencies("/nonexistent/path/plugin.so");
    EXPECT_TRUE(deps.empty());
}

TEST(GetModuleDependenciesTest, EmptyPath_ReturnsEmpty) {
    std::vector<std::string> deps = LogosModule::getModuleDependencies("");
    EXPECT_TRUE(deps.empty());
}

TEST(GetModuleDependenciesTest, InvalidFile_ReturnsEmpty) {
    std::vector<std::string> deps = LogosModule::getModuleDependencies("/dev/null");
    EXPECT_TRUE(deps.empty());
}

// =============================================================================
// Real Plugin Tests for getModuleName / getModuleDependencies
// =============================================================================

class RealPluginMetadataTest : public ::testing::Test {
protected:
    std::string testPlugin;

    void SetUp() override {
#ifdef __APPLE__
        std::string pluginName = "package_manager_plugin.dylib";
#else
        std::string pluginName = "package_manager_plugin.so";
#endif

        const char* envPlugin = std::getenv("TEST_PLUGIN");
        if (envPlugin && std::string(envPlugin).length() > 0) {
            testPlugin = envPlugin;
        } else {
            // TODO: this is dumb, fix
            std::vector<std::string> possiblePaths = {
                "tests/examples/" + pluginName,
                "./tests/examples/" + pluginName,
                "../tests/examples/" + pluginName,
                "../../tests/examples/" + pluginName,
                "../../../tests/examples/" + pluginName,
                "examples/" + pluginName,
            };

            for (const auto& path : possiblePaths) {
                if (std::system(("test -f " + path + " 2>/dev/null").c_str()) == 0) {
                    testPlugin = path;
                    break;
                }
            }
        }

        if (testPlugin.empty()) {
            GTEST_SKIP() << "Test plugin not found. Set TEST_PLUGIN environment variable.";
        }
    }
};

TEST_F(RealPluginMetadataTest, GetModuleName_ReturnsCorrectName) {
    std::string name = LogosModule::getModuleName(testPlugin);
    EXPECT_EQ(name, "package_manager");
}

TEST_F(RealPluginMetadataTest, GetModuleDependencies_ReturnsEmptyForNoDeps) {
    std::vector<std::string> deps = LogosModule::getModuleDependencies(testPlugin);
    EXPECT_TRUE(deps.empty());
}

// =============================================================================
// ModuleMetadata::fromPath(std::string) overload Tests
// =============================================================================

TEST(MetadataFromPathStdStringTest, NonExistentPath_ReturnsNullopt) {
    auto result = ModuleMetadata::fromPath(std::string("/nonexistent/path/plugin.so"));
    EXPECT_FALSE(result.has_value());
}

TEST(MetadataFromPathStdStringTest, EmptyPath_ReturnsNullopt) {
    auto result = ModuleMetadata::fromPath(std::string(""));
    EXPECT_FALSE(result.has_value());
}

TEST(MetadataFromPathStdStringTest, InvalidFile_ReturnsNullopt) {
    auto result = ModuleMetadata::fromPath(std::string("/dev/null"));
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// LogosModule::loadFromPath(std::string, std::string*) overload Tests
// =============================================================================

TEST(LoadFromPathStdStringTest, NonExistentPath_ReturnsInvalidModule) {
    std::string errorString;
    LogosModule module = LogosModule::loadFromPath(
        std::string("/nonexistent/path/plugin.so"), &errorString);

    EXPECT_FALSE(module.isValid());
    EXPECT_FALSE(errorString.empty());
}

TEST(LoadFromPathStdStringTest, NonExistentPath_NoErrorStringPtr_DoesNotCrash) {
    LogosModule module = LogosModule::loadFromPath(
        std::string("/nonexistent/path/plugin.so"));

    EXPECT_FALSE(module.isValid());
}

TEST(LoadFromPathStdStringTest, EmptyPath_ReturnsInvalidModule) {
    std::string errorString;
    LogosModule module = LogosModule::loadFromPath(std::string(""), &errorString);

    EXPECT_FALSE(module.isValid());
    EXPECT_FALSE(errorString.empty());
}

TEST(LoadFromPathStdStringTest, InvalidFile_ReturnsInvalidModuleWithError) {
    std::string errorString;
    LogosModule module = LogosModule::loadFromPath(std::string("/dev/null"), &errorString);

    EXPECT_FALSE(module.isValid());
    EXPECT_FALSE(errorString.empty());
}

// =============================================================================
// LogosModule::extractMetadata(std::string) overload Tests
// =============================================================================

TEST(ExtractMetadataStdStringTest, NonExistentPath_ReturnsNullopt) {
    auto result = LogosModule::extractMetadata(std::string("/nonexistent/path/plugin.so"));
    EXPECT_FALSE(result.has_value());
}

TEST(ExtractMetadataStdStringTest, EmptyPath_ReturnsNullopt) {
    auto result = LogosModule::extractMetadata(std::string(""));
    EXPECT_FALSE(result.has_value());
}

TEST(ExtractMetadataStdStringTest, InvalidFile_ReturnsNullopt) {
    auto result = LogosModule::extractMetadata(std::string("/dev/null"));
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// Real plugin tests for std::string overloads
// =============================================================================

// Do not assert loadFromPath succeeds here: full dlopen can fail in headless CI
// (missing deps, RPATH, no QCoreApplication) while metadata-only APIs still work.
// Instead, verify the std::string overload matches the QString overload exactly.
TEST_F(RealPluginMetadataTest, LoadFromPath_StdString_MatchesQStringOverload) {
    QString qError;
    std::string sError;
    LogosModule viaQString = LogosModule::loadFromPath(QString::fromStdString(testPlugin), &qError);
    LogosModule viaStdString = LogosModule::loadFromPath(testPlugin, &sError);

    EXPECT_EQ(viaQString.isValid(), viaStdString.isValid());
    EXPECT_EQ(qError.toStdString(), sError);
}

TEST_F(RealPluginMetadataTest, ExtractMetadata_StdString_ReturnsMetadata) {
    auto result = LogosModule::extractMetadata(testPlugin);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->isValid());
    EXPECT_EQ(result->name.toStdString(), "package_manager");
}

TEST_F(RealPluginMetadataTest, MetadataFromPath_StdString_ReturnsMetadata) {
    auto result = ModuleMetadata::fromPath(testPlugin);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->isValid());
    EXPECT_EQ(result->name.toStdString(), "package_manager");
}
