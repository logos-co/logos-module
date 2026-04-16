#include <gtest/gtest.h>
#include "logos_module.h"
#include "logos_provider_plugin.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

using namespace ModuleLib;

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

// ---------------------------------------------------------------------------
// Mock for new-API (LogosProviderPlugin) path
// ---------------------------------------------------------------------------

class MockProviderObject : public LogosProviderObject {
public:
    QVariant callMethod(const QString&, const QVariantList&) override { return {}; }
    bool informModuleToken(const QString&, const QString&) override { return true; }
    QJsonArray getMethods() override {
        QJsonArray methods;
        {
            QJsonObject m;
            m["name"] = "providerMethod";
            m["signature"] = "providerMethod(QString)";
            m["returnType"] = "QString";
            m["isInvokable"] = true;
            QJsonArray params;
            params.append(QJsonObject{{"type", "QString"}, {"name", "input"}});
            m["parameters"] = params;
            methods.append(m);
        }
        {
            QJsonObject m;
            m["name"] = "noArgMethod";
            m["signature"] = "noArgMethod()";
            m["returnType"] = "bool";
            m["isInvokable"] = true;
            methods.append(m);
        }
        {
            QJsonObject m;
            m["name"] = "multiParam";
            m["signature"] = "multiParam(QString,int,bool)";
            m["returnType"] = "void";
            m["isInvokable"] = true;
            QJsonArray params;
            params.append(QJsonObject{{"type", "QString"}, {"name", "name"}});
            params.append(QJsonObject{{"type", "int"}, {"name", "count"}});
            params.append(QJsonObject{{"type", "bool"}, {"name", "flag"}});
            m["parameters"] = params;
            methods.append(m);
        }
        return methods;
    }
    void setEventListener(EventCallback) override {}
    void init(void*) override {}
    QString providerName() const override { return "mock_provider"; }
    QString providerVersion() const override { return "1.0.0"; }
};

class MockNewApiPlugin : public QObject, public LogosProviderPlugin {
    Q_OBJECT
    Q_INTERFACES(LogosProviderPlugin)
public:
    explicit MockNewApiPlugin(QObject* parent = nullptr) : QObject(parent) {}
    LogosProviderObject* createProviderObject() override {
        return new MockProviderObject();
    }
};

// A new-API plugin that returns null from createProviderObject —
// should fall back to the legacy QMetaObject path
class MockBrokenNewApiPlugin : public QObject, public LogosProviderPlugin {
    Q_OBJECT
    Q_INTERFACES(LogosProviderPlugin)
public:
    explicit MockBrokenNewApiPlugin(QObject* parent = nullptr) : QObject(parent) {}
    LogosProviderObject* createProviderObject() override {
        return nullptr;
    }

    // Legacy Q_INVOKABLE method — should be found via fallback
    Q_INVOKABLE QString legacyMethod() { return "legacy"; }
};

#include "test_introspection.moc"

// ---------------------------------------------------------------------------
// Legacy Q_INVOKABLE tests (existing)
// ---------------------------------------------------------------------------

TEST(IntrospectionTest, GetMethods_ReturnsPluginMethods) {
    MockPlugin plugin;
    
    auto methods = LogosModule::getMethods(&plugin);
    
    EXPECT_FALSE(methods.empty());
    
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
    
    auto methodsExcluded = LogosModule::getMethods(&plugin, true);
    auto methodsIncluded = LogosModule::getMethods(&plugin, false);
    
    EXPECT_LT(methodsExcluded.size(), methodsIncluded.size());
    
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
    auto methods = LogosModule::getMethods(nullptr);
    
    EXPECT_TRUE(methods.empty());
}

TEST(IntrospectionTest, GetMethods_IncludesSlots) {
    MockPlugin plugin;
    
    auto methods = LogosModule::getMethods(&plugin);
    
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

TEST(IntrospectionTest, GetMethodsAsJson_ReturnsValidJson) {
    MockPlugin plugin;
    
    QJsonArray methodsJson = LogosModule::getMethodsAsJson(&plugin);
    
    EXPECT_FALSE(methodsJson.isEmpty());
    
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
    QJsonArray methodsJson = LogosModule::getMethodsAsJson(nullptr);
    
    EXPECT_TRUE(methodsJson.isEmpty());
}

TEST(IntrospectionTest, GetMethodsAsJson_ParametersIncluded) {
    MockPlugin plugin;
    
    QJsonArray methodsJson = LogosModule::getMethodsAsJson(&plugin);
    
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

TEST(IntrospectionTest, GetClassName_ReturnsCorrectName) {
    MockPlugin plugin;
    
    QString className = LogosModule::getClassName(&plugin);
    
    EXPECT_EQ(className.toStdString(), "MockPlugin");
}

TEST(IntrospectionTest, GetClassName_NullPlugin) {
    QString className = LogosModule::getClassName(nullptr);
    
    EXPECT_TRUE(className.isEmpty());
}

TEST(IntrospectionTest, HasMethod_ExistingMethod) {
    MockPlugin plugin;
    
    EXPECT_TRUE(LogosModule::hasMethod(&plugin, "testMethod"));
    EXPECT_TRUE(LogosModule::hasMethod(&plugin, "noReturnMethod"));
    EXPECT_TRUE(LogosModule::hasMethod(&plugin, "methodWithMultipleParams"));
}

TEST(IntrospectionTest, HasMethod_NonExistingMethod) {
    MockPlugin plugin;
    
    EXPECT_FALSE(LogosModule::hasMethod(&plugin, "nonExistentMethod"));
    EXPECT_FALSE(LogosModule::hasMethod(&plugin, ""));
}

TEST(IntrospectionTest, HasMethod_NullPlugin) {
    EXPECT_FALSE(LogosModule::hasMethod(nullptr, "testMethod"));
}

TEST(IntrospectionTest, HasMethod_BaseClassMethod) {
    MockPlugin plugin;
    
    EXPECT_TRUE(LogosModule::hasMethod(&plugin, "deleteLater"));
}

TEST(IntrospectionTest, ParameterInfo_ToJson) {
    ParameterInfo param;
    param.name = "testParam";
    param.type = "QString";
    
    QJsonObject json = param.toJson();
    
    EXPECT_EQ(json["name"].toString().toStdString(), "testParam");
    EXPECT_EQ(json["type"].toString().toStdString(), "QString");
}

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
    EXPECT_FALSE(json.contains("parameters"));
}

// ---------------------------------------------------------------------------
// New-API (LogosProviderPlugin) introspection tests
// ---------------------------------------------------------------------------

TEST(ProviderPluginIntrospectionTest, GetMethods_ReturnsProviderMethods) {
    MockNewApiPlugin plugin;

    auto methods = LogosModule::getMethods(&plugin);

    ASSERT_EQ(methods.size(), 3);

    EXPECT_EQ(methods[0].name, "providerMethod");
    EXPECT_EQ(methods[0].returnType, "QString");
    EXPECT_EQ(methods[0].signature, "providerMethod(QString)");
    EXPECT_TRUE(methods[0].isInvokable);
    ASSERT_EQ(methods[0].parameters.size(), 1);
    EXPECT_EQ(methods[0].parameters[0].name, "input");
    EXPECT_EQ(methods[0].parameters[0].type, "QString");

    EXPECT_EQ(methods[1].name, "noArgMethod");
    EXPECT_EQ(methods[1].returnType, "bool");
    EXPECT_TRUE(methods[1].parameters.empty());

    EXPECT_EQ(methods[2].name, "multiParam");
    EXPECT_EQ(methods[2].returnType, "void");
    ASSERT_EQ(methods[2].parameters.size(), 3);
    EXPECT_EQ(methods[2].parameters[0].name, "name");
    EXPECT_EQ(methods[2].parameters[1].name, "count");
    EXPECT_EQ(methods[2].parameters[2].name, "flag");
}

TEST(ProviderPluginIntrospectionTest, GetMethods_DoesNotReturnQObjectMethods) {
    MockNewApiPlugin plugin;

    auto methods = LogosModule::getMethods(&plugin);

    // New-API path returns exactly what the provider reports — no QObject base methods
    for (const auto& m : methods) {
        EXPECT_NE(m.name, "deleteLater");
        EXPECT_NE(m.name, "objectName");
        EXPECT_NE(m.name, "destroyed");
    }
}

TEST(ProviderPluginIntrospectionTest, GetMethodsAsJson_ReturnsProviderJson) {
    MockNewApiPlugin plugin;

    QJsonArray methodsJson = LogosModule::getMethodsAsJson(&plugin);

    ASSERT_EQ(methodsJson.size(), 3);

    QJsonObject first = methodsJson[0].toObject();
    EXPECT_EQ(first["name"].toString(), "providerMethod");
    EXPECT_EQ(first["returnType"].toString(), "QString");
    EXPECT_EQ(first["signature"].toString(), "providerMethod(QString)");
    EXPECT_TRUE(first["isInvokable"].toBool());
    ASSERT_TRUE(first.contains("parameters"));
    EXPECT_EQ(first["parameters"].toArray().size(), 1);
}

TEST(ProviderPluginIntrospectionTest, GetMethodsAsJson_MatchesGetMethods) {
    MockNewApiPlugin plugin;

    auto methods = LogosModule::getMethods(&plugin);
    QJsonArray methodsJson = LogosModule::getMethodsAsJson(&plugin);

    // Both paths should return the same number of methods
    ASSERT_EQ(static_cast<int>(methods.size()), methodsJson.size());

    // Names should match
    for (int i = 0; i < methodsJson.size(); ++i) {
        EXPECT_EQ(methods[i].name, methodsJson[i].toObject()["name"].toString());
    }
}

TEST(ProviderPluginIntrospectionTest, ExcludeBaseClass_Ignored) {
    MockNewApiPlugin plugin;

    // excludeBaseClass flag has no effect on new-API path (there is no QMetaObject)
    auto withExclude = LogosModule::getMethods(&plugin, true);
    auto withoutExclude = LogosModule::getMethods(&plugin, false);

    EXPECT_EQ(withExclude.size(), withoutExclude.size());
}

TEST(ProviderPluginIntrospectionTest, BrokenProvider_FallsBackToLegacy) {
    MockBrokenNewApiPlugin plugin;

    // createProviderObject() returns nullptr, so getMethods should
    // fall through to the legacy QMetaObject path
    auto methods = LogosModule::getMethods(&plugin);

    bool foundLegacyMethod = false;
    for (const auto& m : methods) {
        if (m.name == "legacyMethod") foundLegacyMethod = true;
    }
    EXPECT_TRUE(foundLegacyMethod);
}

TEST(ProviderPluginIntrospectionTest, BrokenProvider_FallsBackToLegacyJson) {
    MockBrokenNewApiPlugin plugin;

    QJsonArray methodsJson = LogosModule::getMethodsAsJson(&plugin);

    bool foundLegacyMethod = false;
    for (const QJsonValue& v : methodsJson) {
        if (v.toObject()["name"].toString() == "legacyMethod")
            foundLegacyMethod = true;
    }
    EXPECT_TRUE(foundLegacyMethod);
}

TEST(ProviderPluginIntrospectionTest, HasMethod_WorksWithProvider) {
    MockNewApiPlugin plugin;

    EXPECT_TRUE(LogosModule::hasMethod(&plugin, "providerMethod"));
    EXPECT_TRUE(LogosModule::hasMethod(&plugin, "noArgMethod"));
    EXPECT_TRUE(LogosModule::hasMethod(&plugin, "multiParam"));
    EXPECT_FALSE(LogosModule::hasMethod(&plugin, "nonExistent"));
}

TEST(ProviderPluginIntrospectionTest, LegacyPlugin_StillWorks) {
    // Verify the legacy path is unaffected by the new-API code
    MockPlugin legacyPlugin;

    auto methods = LogosModule::getMethods(&legacyPlugin, true);

    bool foundTestMethod = false;
    for (const auto& m : methods) {
        if (m.name == "testMethod") foundTestMethod = true;
    }
    EXPECT_TRUE(foundTestMethod);
}

// ---------------------------------------------------------------------------
// hasMethod(std::string) instance-method overload tests
// ---------------------------------------------------------------------------

TEST(IntrospectionStdStringTest, HasMethod_StdString_ExistingMethod) {
    MockPlugin plugin;
    LogosModule module = LogosModule::wrapExisting(&plugin);

    EXPECT_TRUE(module.hasMethod(std::string("testMethod")));
    EXPECT_TRUE(module.hasMethod(std::string("noReturnMethod")));
    EXPECT_TRUE(module.hasMethod(std::string("methodWithMultipleParams")));
}

TEST(IntrospectionStdStringTest, HasMethod_StdString_NonExistingMethod) {
    MockPlugin plugin;
    LogosModule module = LogosModule::wrapExisting(&plugin);

    EXPECT_FALSE(module.hasMethod(std::string("nonExistentMethod")));
    EXPECT_FALSE(module.hasMethod(std::string("")));
}

TEST(IntrospectionStdStringTest, HasMethod_StdString_MatchesQStringOverload) {
    MockPlugin plugin;
    LogosModule module = LogosModule::wrapExisting(&plugin);

    // Both overloads must agree on the same answers
    EXPECT_EQ(module.hasMethod(std::string("testMethod")),
              module.hasMethod(QString("testMethod")));
    EXPECT_EQ(module.hasMethod(std::string("nonExistent")),
              module.hasMethod(QString("nonExistent")));
}

TEST(IntrospectionStdStringTest, HasMethod_StdString_ProviderPlugin) {
    MockNewApiPlugin plugin;
    LogosModule module = LogosModule::wrapExisting(&plugin);

    EXPECT_TRUE(module.hasMethod(std::string("providerMethod")));
    EXPECT_TRUE(module.hasMethod(std::string("noArgMethod")));
    EXPECT_FALSE(module.hasMethod(std::string("nonExistent")));
}
