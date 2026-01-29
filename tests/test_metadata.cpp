#include <gtest/gtest.h>
#include "module_metadata.h"
#include <QJsonArray>

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
