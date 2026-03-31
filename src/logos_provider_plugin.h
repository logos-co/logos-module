// Minimal abstract interfaces for new-API plugin detection in lm.
// Vtable layout MUST match logos-cpp-sdk/cpp/logos_provider_object.h.
#ifndef LOGOS_PROVIDER_PLUGIN_H
#define LOGOS_PROVIDER_PLUGIN_H

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QJsonArray>
#include <QtPlugin>
#include <functional>

class LogosProviderObject {
public:
    virtual ~LogosProviderObject() = default;

    using EventCallback = std::function<void(const QString&, const QVariantList&)>;

    virtual QVariant callMethod(const QString& methodName, const QVariantList& args) = 0;
    virtual bool informModuleToken(const QString& moduleName, const QString& token) = 0;
    virtual QJsonArray getMethods() = 0;
    virtual void setEventListener(EventCallback callback) = 0;
    virtual void init(void* apiInstance) = 0;
    virtual QString providerName() const = 0;
    virtual QString providerVersion() const = 0;
};

class LogosProviderPlugin {
public:
    virtual ~LogosProviderPlugin() = default;
    virtual LogosProviderObject* createProviderObject() = 0;
};

#define LogosProviderPlugin_iid "org.logos.LogosProviderPlugin"
Q_DECLARE_INTERFACE(LogosProviderPlugin, LogosProviderPlugin_iid)

#endif // LOGOS_PROVIDER_PLUGIN_H
