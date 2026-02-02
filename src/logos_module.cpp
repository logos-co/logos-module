#include "logos_module.h"
#include <QMetaObject>
#include <QMetaMethod>
#include <QDebug>

namespace ModuleLib {

QJsonObject ParameterInfo::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["type"] = type;
    return obj;
}

QJsonObject MethodInfo::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["signature"] = signature;
    obj["returnType"] = returnType;
    obj["isInvokable"] = isInvokable;
    
    if (!parameters.empty()) {
        QJsonArray paramsArray;
        for (const auto& param : parameters) {
            paramsArray.append(param.toJson());
        }
        obj["parameters"] = paramsArray;
    }
    
    return obj;
}

LogosModule::LogosModule() = default;

LogosModule::~LogosModule() {
    unload();
}

LogosModule::LogosModule(LogosModule&& other) noexcept
    : m_loader(other.m_loader)
    , m_instance(other.m_instance)
    , m_metadata(std::move(other.m_metadata))
    , m_errorString(std::move(other.m_errorString))
    , m_isStatic(other.m_isStatic)
{
    other.m_loader = nullptr;
    other.m_instance = nullptr;
    other.m_isStatic = false;
}

LogosModule& LogosModule::operator=(LogosModule&& other) noexcept {
    if (this != &other) {
        unload();
        
        m_loader = other.m_loader;
        m_instance = other.m_instance;
        m_metadata = std::move(other.m_metadata);
        m_errorString = std::move(other.m_errorString);
        m_isStatic = other.m_isStatic;
        
        other.m_loader = nullptr;
        other.m_instance = nullptr;
        other.m_isStatic = false;
    }
    return *this;
}

LogosModule LogosModule::loadFromPath(const QString& pluginPath, QString* errorString) {
    LogosModule module;
    
    module.m_loader = new QPluginLoader(pluginPath);
    
    QJsonObject rawMetadata = module.m_loader->metaData();
    if (!rawMetadata.isEmpty()) {
        auto metadata = ModuleMetadata::fromJson(rawMetadata);
        if (metadata) {
            module.m_metadata = *metadata;
        }
    }
    
    module.m_instance = module.m_loader->instance();
    
    if (!module.m_instance) {
        module.m_errorString = module.m_loader->errorString();
        if (errorString) {
            *errorString = module.m_errorString;
        }
        qWarning() << "LogosModule: Failed to load plugin:" << pluginPath 
                   << "Error:" << module.m_errorString;
        
        delete module.m_loader;
        module.m_loader = nullptr;
        return module;
    }
    
    qDebug() << "LogosModule: Plugin loaded successfully:" << pluginPath;
    
    return module;
}

std::vector<LogosModule> LogosModule::getStaticModules() {
    std::vector<LogosModule> modules;
    
    const QObjectList staticPlugins = QPluginLoader::staticInstances();
    qDebug() << "LogosModule: Found" << staticPlugins.size() << "static plugin instances";
    
    for (QObject* pluginObject : staticPlugins) {
        if (!pluginObject) {
            continue;
        }
        
        LogosModule module = wrapExisting(pluginObject);
        module.m_isStatic = true;
        
        if (module.isValid()) {
            modules.push_back(std::move(module));
        }
    }
    
    return modules;
}

LogosModule LogosModule::wrapExisting(QObject* pluginObject, const ModuleMetadata& metadata) {
    LogosModule module;
    
    if (!pluginObject) {
        module.m_errorString = "Null plugin object";
        return module;
    }
    
    module.m_instance = pluginObject;
    module.m_metadata = metadata;
    module.m_isStatic = true;
    module.m_loader = nullptr;
    
    return module;
}

std::optional<ModuleMetadata> LogosModule::extractMetadata(const QString& pluginPath) {
    return ModuleMetadata::fromPath(pluginPath);
}

bool LogosModule::isValid() const {
    return m_instance != nullptr;
}

QObject* LogosModule::instance() const {
    return m_instance;
}

const ModuleMetadata& LogosModule::metadata() const {
    return m_metadata;
}

QString LogosModule::errorString() const {
    return m_errorString;
}

void LogosModule::unload() {
    if (m_loader && !m_isStatic) {
        m_loader->unload();
        delete m_loader;
    }
    m_loader = nullptr;
    m_instance = nullptr;
}

QObject* LogosModule::release() {
    QObject* instance = m_instance;
    
    m_loader = nullptr;
    m_instance = nullptr;
    m_isStatic = true;
    
    return instance;
}

std::vector<MethodInfo> LogosModule::getMethods(bool excludeBaseClass) const {
    return getMethods(m_instance, excludeBaseClass);
}

QJsonArray LogosModule::getMethodsAsJson(bool excludeBaseClass) const {
    return getMethodsAsJson(m_instance, excludeBaseClass);
}

QString LogosModule::getClassName() const {
    return getClassName(m_instance);
}

bool LogosModule::hasMethod(const QString& methodName) const {
    return hasMethod(m_instance, methodName);
}

std::vector<MethodInfo> LogosModule::getMethods(QObject* obj, bool excludeBaseClass) {
    std::vector<MethodInfo> methods;
    
    if (!obj) {
        qWarning() << "LogosModule: Null object for introspection";
        return methods;
    }
    
    const QMetaObject* metaObject = obj->metaObject();
    
    for (int i = 0; i < metaObject->methodCount(); ++i) {
        QMetaMethod method = metaObject->method(i);
        
        if (excludeBaseClass && method.enclosingMetaObject() != metaObject) {
            continue;
        }
        
        MethodInfo info;
        info.signature = QString::fromUtf8(method.methodSignature());
        info.name = QString::fromUtf8(method.name());
        info.returnType = QString::fromUtf8(method.typeName());
        
        info.isInvokable = method.isValid() && 
                          (method.methodType() == QMetaMethod::Method || 
                           method.methodType() == QMetaMethod::Slot);
        
        if (method.parameterCount() > 0) {
            QByteArrayList paramNames = method.parameterNames();
            
            for (int p = 0; p < method.parameterCount(); ++p) {
                ParameterInfo param;
                param.type = QString::fromUtf8(method.parameterTypeName(p));
                
                if (p < paramNames.size() && !paramNames.at(p).isEmpty()) {
                    param.name = QString::fromUtf8(paramNames.at(p));
                } else {
                    param.name = "param" + QString::number(p);
                }
                
                info.parameters.push_back(param);
            }
        }
        
        methods.push_back(info);
    }
    
    return methods;
}

QJsonArray LogosModule::getMethodsAsJson(QObject* obj, bool excludeBaseClass) {
    QJsonArray methodsArray;
    
    auto methods = getMethods(obj, excludeBaseClass);
    for (const auto& method : methods) {
        methodsArray.append(method.toJson());
    }
    
    return methodsArray;
}

QString LogosModule::getClassName(QObject* obj) {
    if (!obj) {
        return QString();
    }
    return QString::fromUtf8(obj->metaObject()->className());
}

bool LogosModule::hasMethod(QObject* obj, const QString& methodName) {
    if (!obj) {
        return false;
    }
    
    auto methods = getMethods(obj, false);
    for (const auto& method : methods) {
        if (method.name == methodName) {
            return true;
        }
    }
    
    return false;
}

} // namespace ModuleLib
