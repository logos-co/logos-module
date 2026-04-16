#include <gtest/gtest.h>
#include "instance_persistence.h"
#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>

using namespace ModuleLib::InstancePersistence;

class InstancePersistenceTest : public ::testing::Test {
protected:
    QTemporaryDir tmpDir;

    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());
    }

    QString basePath() const { return tmpDir.path(); }
};

// =============================================================================
// ReuseOrCreate mode
// =============================================================================

TEST_F(InstancePersistenceTest, ReuseOrCreate_CreatesNewWhenNoneExist) {
    auto info = resolveInstance(basePath(), "my_module");

    EXPECT_FALSE(info.instanceId.isEmpty());
    EXPECT_EQ(info.instanceId.length(), 12);
    EXPECT_TRUE(QDir(info.persistencePath).exists());
    EXPECT_EQ(info.persistencePath.toStdString(),
              (basePath() + "/my_module/" + info.instanceId).toStdString());
}

TEST_F(InstancePersistenceTest, ReuseOrCreate_ReusesExistingInstance) {
    // Create an instance first
    auto first = resolveInstance(basePath(), "my_module");
    ASSERT_FALSE(first.instanceId.isEmpty());

    // Resolve again — should reuse
    auto second = resolveInstance(basePath(), "my_module");
    EXPECT_EQ(first.instanceId.toStdString(), second.instanceId.toStdString());
    EXPECT_EQ(first.persistencePath.toStdString(), second.persistencePath.toStdString());
}

TEST_F(InstancePersistenceTest, ReuseOrCreate_ReusesFirstWhenMultipleExist) {
    // Create two instances via AlwaysCreate
    auto first = resolveInstance(basePath(), "my_module", ResolveMode::AlwaysCreate);
    auto second = resolveInstance(basePath(), "my_module", ResolveMode::AlwaysCreate);
    ASSERT_NE(first.instanceId.toStdString(), second.instanceId.toStdString());

    // ReuseOrCreate should pick the first one alphabetically
    auto resolved = resolveInstance(basePath(), "my_module", ResolveMode::ReuseOrCreate);
    QDir moduleDir(basePath() + "/my_module");
    QStringList entries = moduleDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    ASSERT_FALSE(entries.isEmpty());
    EXPECT_EQ(resolved.instanceId.toStdString(), entries.first().toStdString());
}

// =============================================================================
// AlwaysCreate mode
// =============================================================================

TEST_F(InstancePersistenceTest, AlwaysCreate_GeneratesNewIdEachTime) {
    auto first = resolveInstance(basePath(), "my_module", ResolveMode::AlwaysCreate);
    auto second = resolveInstance(basePath(), "my_module", ResolveMode::AlwaysCreate);

    EXPECT_FALSE(first.instanceId.isEmpty());
    EXPECT_FALSE(second.instanceId.isEmpty());
    EXPECT_NE(first.instanceId.toStdString(), second.instanceId.toStdString());
    EXPECT_TRUE(QDir(first.persistencePath).exists());
    EXPECT_TRUE(QDir(second.persistencePath).exists());
}

TEST_F(InstancePersistenceTest, AlwaysCreate_IgnoresExistingDirectories) {
    // Pre-create an instance
    auto existing = resolveInstance(basePath(), "my_module");
    ASSERT_FALSE(existing.instanceId.isEmpty());

    // AlwaysCreate should still make a new one
    auto fresh = resolveInstance(basePath(), "my_module", ResolveMode::AlwaysCreate);
    EXPECT_NE(existing.instanceId.toStdString(), fresh.instanceId.toStdString());
}

// =============================================================================
// UseExplicit mode
// =============================================================================

TEST_F(InstancePersistenceTest, UseExplicit_UsesProvidedId) {
    auto info = resolveInstance(basePath(), "my_module",
                                ResolveMode::UseExplicit, "custom_id_123");

    EXPECT_EQ(info.instanceId.toStdString(), "custom_id_123");
    EXPECT_EQ(info.persistencePath.toStdString(),
              (basePath() + "/my_module/custom_id_123").toStdString());
    EXPECT_TRUE(QDir(info.persistencePath).exists());
}

TEST_F(InstancePersistenceTest, UseExplicit_CreatesDirectoryIfNeeded) {
    QString expectedPath = basePath() + "/new_module/explicit_id";
    ASSERT_FALSE(QDir(expectedPath).exists());

    auto info = resolveInstance(basePath(), "new_module",
                                ResolveMode::UseExplicit, "explicit_id");
    EXPECT_TRUE(QDir(expectedPath).exists());
}

TEST_F(InstancePersistenceTest, UseExplicit_FailsWithEmptyId) {
    auto info = resolveInstance(basePath(), "my_module",
                                ResolveMode::UseExplicit, "");

    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

// =============================================================================
// Error handling
// =============================================================================

TEST_F(InstancePersistenceTest, EmptyBasePath_ReturnsEmpty) {
    auto info = resolveInstance(QString(""), "my_module");
    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

TEST_F(InstancePersistenceTest, EmptyModuleName_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), "");
    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

// =============================================================================
// Path traversal protection
// =============================================================================

TEST_F(InstancePersistenceTest, ModuleNameWithSlash_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), "../escape");
    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

TEST_F(InstancePersistenceTest, ModuleNameWithPathSeparator_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), "foo/bar");
    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

TEST_F(InstancePersistenceTest, ModuleNameDotDot_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), "..");
    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

TEST_F(InstancePersistenceTest, ExplicitIdWithPathTraversal_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), "my_module",
                                ResolveMode::UseExplicit, "../escape");
    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

TEST_F(InstancePersistenceTest, ExplicitIdWithSlash_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), "my_module",
                                ResolveMode::UseExplicit, "foo/bar");
    EXPECT_TRUE(info.instanceId.isEmpty());
    EXPECT_TRUE(info.persistencePath.isEmpty());
}

// =============================================================================
// std::string overload
// =============================================================================

class StdStringInstancePersistenceTest : public ::testing::Test {
protected:
    QTemporaryDir tmpDir;

    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());
    }

    std::string basePath() const { return tmpDir.path().toStdString(); }
};

TEST_F(StdStringInstancePersistenceTest, StdString_ReuseOrCreate_CreatesNewInstance) {
    auto info = resolveInstance(basePath(), "my_module");

    EXPECT_FALSE(info.instanceId.empty());
    EXPECT_EQ(info.instanceId.size(), 12u);
    EXPECT_TRUE(QDir(QString::fromStdString(info.persistencePath)).exists());
    EXPECT_EQ(info.persistencePath,
              basePath() + "/my_module/" + info.instanceId);
}

TEST_F(StdStringInstancePersistenceTest, StdString_ReuseOrCreate_ReusesExistingInstance) {
    auto first = resolveInstance(basePath(), "my_module");
    ASSERT_FALSE(first.instanceId.empty());

    auto second = resolveInstance(basePath(), "my_module");
    EXPECT_EQ(first.instanceId, second.instanceId);
    EXPECT_EQ(first.persistencePath, second.persistencePath);
}

TEST_F(StdStringInstancePersistenceTest, StdString_AlwaysCreate_GeneratesNewIdEachTime) {
    auto first = resolveInstance(basePath(), "my_module", ResolveMode::AlwaysCreate);
    auto second = resolveInstance(basePath(), "my_module", ResolveMode::AlwaysCreate);

    EXPECT_FALSE(first.instanceId.empty());
    EXPECT_FALSE(second.instanceId.empty());
    EXPECT_NE(first.instanceId, second.instanceId);
}

TEST_F(StdStringInstancePersistenceTest, StdString_UseExplicit_UsesProvidedId) {
    auto info = resolveInstance(basePath(), "my_module",
                                ResolveMode::UseExplicit, "custom_id_123");

    EXPECT_EQ(info.instanceId, "custom_id_123");
    EXPECT_EQ(info.persistencePath,
              basePath() + "/my_module/custom_id_123");
    EXPECT_TRUE(QDir(QString::fromStdString(info.persistencePath)).exists());
}

TEST_F(StdStringInstancePersistenceTest, StdString_EmptyBasePath_ReturnsEmpty) {
    auto info = resolveInstance(std::string(""), "my_module");
    EXPECT_TRUE(info.instanceId.empty());
    EXPECT_TRUE(info.persistencePath.empty());
}

TEST_F(StdStringInstancePersistenceTest, StdString_EmptyModuleName_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), std::string(""));
    EXPECT_TRUE(info.instanceId.empty());
    EXPECT_TRUE(info.persistencePath.empty());
}

TEST_F(StdStringInstancePersistenceTest, StdString_PathTraversal_ReturnsEmpty) {
    auto info = resolveInstance(basePath(), "../escape");
    EXPECT_TRUE(info.instanceId.empty());
    EXPECT_TRUE(info.persistencePath.empty());
}

TEST_F(StdStringInstancePersistenceTest, StdString_ResultMatchesQStringOverload) {
    auto stdInfo = resolveInstance(basePath(), "my_module",
                                   ResolveMode::UseExplicit, "test_id");
    auto qtInfo = resolveInstance(QString::fromStdString(basePath()),
                                  "my_module",
                                  ResolveMode::UseExplicit, "test_id");

    EXPECT_EQ(stdInfo.instanceId, qtInfo.instanceId.toStdString());
    EXPECT_EQ(stdInfo.persistencePath, qtInfo.persistencePath.toStdString());
}

// =============================================================================
// Cross-module isolation
// =============================================================================

TEST_F(InstancePersistenceTest, DifferentModulesGetSeparateDirectories) {
    auto infoA = resolveInstance(basePath(), "module_a");
    auto infoB = resolveInstance(basePath(), "module_b");

    EXPECT_NE(infoA.persistencePath.toStdString(), infoB.persistencePath.toStdString());
    EXPECT_TRUE(infoA.persistencePath.contains("module_a"));
    EXPECT_TRUE(infoB.persistencePath.contains("module_b"));
}

// =============================================================================
// Instance ID format
// =============================================================================

TEST_F(InstancePersistenceTest, GeneratedIdIs12HexChars) {
    auto info = resolveInstance(basePath(), "my_module");
    ASSERT_EQ(info.instanceId.length(), 12);

    // Verify all characters are hex
    QRegularExpression hexPattern("^[0-9a-f]{12}$");
    EXPECT_TRUE(hexPattern.match(info.instanceId).hasMatch())
        << "Instance ID should be 12 hex chars, got: " << info.instanceId.toStdString();
}
