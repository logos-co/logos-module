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
    // NOTE: getEvents() must stay here (right after getMethods, before
    // setEventListener) to match the vtable slot in cpp-sdk's
    // LogosProviderObject — lm dispatches getEvents() through this layout.
    // Non-pure with a default (matching cpp-sdk) so existing implementors
    // (e.g. the unit tests' MockProviderObject) keep compiling without an
    // override; providers built against the events-aware SDK override it.
    virtual QJsonArray getEvents() { return QJsonArray(); }
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
