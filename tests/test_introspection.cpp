#include <gtest/gtest.h>
#include "module_introspection.h"
#include <QCoreApplication>
#include <QJsonDocument>

using namespace ModuleLib;

// =============================================================================
// Mock Plugin Class for Testing
// =============================================================================

class MockPlugin : public QObject {
    Q_OBJECT
public:
    explicit MockPlugin(QObject* parent = nullptr) : QObject(parent) {}
    
    Q_INVOKABLE QString testMethod(int value) {
        return QString::number(value);
    }
    
    Q_INVOKABLE void noReturnMethod() {}
    
    Q_INVOKABLE bool methodWithMultipleParams(QString name, int count, bool flag) {
        Q_UNUSED(name);
        Q_UNUSED(count);
        return flag;
    }
    
public slots:
    void slotMethod() {}
    QString slotWithReturn(int x) { return QString::number(x * 2); }
};

// Include the moc file for MockPlugin
#include "test_introspection.moc"

// =============================================================================
// getMethods Tests
// =============================================================================

TEST(IntrospectionTest, GetMethods_ReturnsPluginMethods) {
    MockPlugin plugin;
    
    auto methods = ModuleIntrospection::getMethods(&plugin);
    
    EXPECT_FALSE(methods.empty());
    
    // Check that our custom methods are found
    bool foundTestMethod = false;
    bool foundNoReturnMethod = false;
    bool foundMultipleParams = false;
    
    for (const auto& method : methods) {
        if (method.name == "testMethod") {
            foundTestMethod = true;
            EXPECT_EQ(method.returnType.toStdString(), "QString");
            EXPECT_EQ(method.parameters.size(), 1);
            if (!method.parameters.empty()) {
                EXPECT_EQ(method.parameters[0].type.toStdString(), "int");
            }
        }
        if (method.name == "noReturnMethod") {
            foundNoReturnMethod = true;
            // void return type may be empty or "void" depending on Qt version
            EXPECT_TRUE(method.returnType.isEmpty() || method.returnType == "void");
            EXPECT_TRUE(method.parameters.empty());
        }
        if (method.name == "methodWithMultipleParams") {
            foundMultipleParams = true;
            EXPECT_EQ(method.returnType.toStdString(), "bool");
            EXPECT_EQ(method.parameters.size(), 3);
        }
    }
    
    EXPECT_TRUE(foundTestMethod);
    EXPECT_TRUE(foundNoReturnMethod);
    EXPECT_TRUE(foundMultipleParams);
}

TEST(IntrospectionTest, GetMethods_ExcludesBaseClass) {
    MockPlugin plugin;
    
    auto methodsExcluded = ModuleIntrospection::getMethods(&plugin, true);
    auto methodsIncluded = ModuleIntrospection::getMethods(&plugin, false);
    
    // When excluding base class, we should have fewer methods
    EXPECT_LT(methodsExcluded.size(), methodsIncluded.size());
    
    // Base class methods like deleteLater should only be in the included set
    bool foundDeleteLaterExcluded = false;
    bool foundDeleteLaterIncluded = false;
    
    for (const auto& method : methodsExcluded) {
        if (method.name == "deleteLater") {
            foundDeleteLaterExcluded = true;
        }
    }
    
    for (const auto& method : methodsIncluded) {
        if (method.name == "deleteLater") {
            foundDeleteLaterIncluded = true;
        }
    }
    
    EXPECT_FALSE(foundDeleteLaterExcluded);
    EXPECT_TRUE(foundDeleteLaterIncluded);
}

TEST(IntrospectionTest, GetMethods_NullPlugin) {
    auto methods = ModuleIntrospection::getMethods(nullptr);
    
    EXPECT_TRUE(methods.empty());
}

TEST(IntrospectionTest, GetMethods_IncludesSlots) {
    MockPlugin plugin;
    
    auto methods = ModuleIntrospection::getMethods(&plugin);
    
    bool foundSlotMethod = false;
    bool foundSlotWithReturn = false;
    
    for (const auto& method : methods) {
        if (method.name == "slotMethod") {
            foundSlotMethod = true;
        }
        if (method.name == "slotWithReturn") {
            foundSlotWithReturn = true;
        }
    }
    
    EXPECT_TRUE(foundSlotMethod);
    EXPECT_TRUE(foundSlotWithReturn);
}

// =============================================================================
// getMethodsAsJson Tests
// =============================================================================

TEST(IntrospectionTest, GetMethodsAsJson_ReturnsValidJson) {
    MockPlugin plugin;
    
    QJsonArray methodsJson = ModuleIntrospection::getMethodsAsJson(&plugin);
    
    EXPECT_FALSE(methodsJson.isEmpty());
    
    // Verify JSON structure
    for (const QJsonValue& value : methodsJson) {
        EXPECT_TRUE(value.isObject());
        QJsonObject methodObj = value.toObject();
        
        EXPECT_TRUE(methodObj.contains("name"));
        EXPECT_TRUE(methodObj.contains("signature"));
        EXPECT_TRUE(methodObj.contains("returnType"));
        EXPECT_TRUE(methodObj.contains("isInvokable"));
    }
}

TEST(IntrospectionTest, GetMethodsAsJson_NullPlugin) {
    QJsonArray methodsJson = ModuleIntrospection::getMethodsAsJson(nullptr);
    
    EXPECT_TRUE(methodsJson.isEmpty());
}

TEST(IntrospectionTest, GetMethodsAsJson_ParametersIncluded) {
    MockPlugin plugin;
    
    QJsonArray methodsJson = ModuleIntrospection::getMethodsAsJson(&plugin);
    
    bool foundMethodWithParams = false;
    
    for (const QJsonValue& value : methodsJson) {
        QJsonObject methodObj = value.toObject();
        if (methodObj["name"].toString() == "methodWithMultipleParams") {
            foundMethodWithParams = true;
            EXPECT_TRUE(methodObj.contains("parameters"));
            QJsonArray params = methodObj["parameters"].toArray();
            EXPECT_EQ(params.size(), 3);
        }
    }
    
    EXPECT_TRUE(foundMethodWithParams);
}

// =============================================================================
// getClassName Tests
// =============================================================================

TEST(IntrospectionTest, GetClassName_ReturnsCorrectName) {
    MockPlugin plugin;
    
    QString className = ModuleIntrospection::getClassName(&plugin);
    
    EXPECT_EQ(className.toStdString(), "MockPlugin");
}

TEST(IntrospectionTest, GetClassName_NullPlugin) {
    QString className = ModuleIntrospection::getClassName(nullptr);
    
    EXPECT_TRUE(className.isEmpty());
}

// =============================================================================
// hasMethod Tests
// =============================================================================

TEST(IntrospectionTest, HasMethod_ExistingMethod) {
    MockPlugin plugin;
    
    EXPECT_TRUE(ModuleIntrospection::hasMethod(&plugin, "testMethod"));
    EXPECT_TRUE(ModuleIntrospection::hasMethod(&plugin, "noReturnMethod"));
    EXPECT_TRUE(ModuleIntrospection::hasMethod(&plugin, "methodWithMultipleParams"));
}

TEST(IntrospectionTest, HasMethod_NonExistingMethod) {
    MockPlugin plugin;
    
    EXPECT_FALSE(ModuleIntrospection::hasMethod(&plugin, "nonExistentMethod"));
    EXPECT_FALSE(ModuleIntrospection::hasMethod(&plugin, ""));
}

TEST(IntrospectionTest, HasMethod_NullPlugin) {
    EXPECT_FALSE(ModuleIntrospection::hasMethod(nullptr, "testMethod"));
}

TEST(IntrospectionTest, HasMethod_BaseClassMethod) {
    MockPlugin plugin;
    
    // deleteLater is a QObject method, should be found when not excluding base
    EXPECT_TRUE(ModuleIntrospection::hasMethod(&plugin, "deleteLater"));
}

// =============================================================================
// ParameterInfo::toJson Tests
// =============================================================================

TEST(IntrospectionTest, ParameterInfo_ToJson) {
    ParameterInfo param;
    param.name = "testParam";
    param.type = "QString";
    
    QJsonObject json = param.toJson();
    
    EXPECT_EQ(json["name"].toString().toStdString(), "testParam");
    EXPECT_EQ(json["type"].toString().toStdString(), "QString");
}

// =============================================================================
// MethodInfo::toJson Tests
// =============================================================================

TEST(IntrospectionTest, MethodInfo_ToJson) {
    MethodInfo method;
    method.name = "testMethod";
    method.signature = "testMethod(int)";
    method.returnType = "QString";
    method.isInvokable = true;
    
    ParameterInfo param;
    param.name = "value";
    param.type = "int";
    method.parameters.push_back(param);
    
    QJsonObject json = method.toJson();
    
    EXPECT_EQ(json["name"].toString().toStdString(), "testMethod");
    EXPECT_EQ(json["signature"].toString().toStdString(), "testMethod(int)");
    EXPECT_EQ(json["returnType"].toString().toStdString(), "QString");
    EXPECT_TRUE(json["isInvokable"].toBool());
    EXPECT_TRUE(json.contains("parameters"));
    EXPECT_EQ(json["parameters"].toArray().size(), 1);
}

TEST(IntrospectionTest, MethodInfo_ToJson_NoParameters) {
    MethodInfo method;
    method.name = "noParamMethod";
    method.signature = "noParamMethod()";
    method.returnType = "void";
    method.isInvokable = true;
    
    QJsonObject json = method.toJson();
    
    EXPECT_EQ(json["name"].toString().toStdString(), "noParamMethod");
    // Empty parameters shouldn't be in JSON
    EXPECT_FALSE(json.contains("parameters"));
}
