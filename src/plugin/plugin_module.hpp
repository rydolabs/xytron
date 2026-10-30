#ifndef NATIVEWEB_PLUGIN_MODULE_HPP_INCLUDED
#define NATIVEWEB_PLUGIN_MODULE_HPP_INCLUDED

#include "core/binding_registry.hpp"
#include "core/native_object_runtime.hpp"
#include <map>
#include <mutex>
#include "nativeweb/plugin_abi.h"

#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace nativeweb {
namespace detail {

class PluginLibraryLease;
struct PluginEventState;

class PluginModule
{
public:
    PluginModule(
        const std::string& path,
        BindingRegistry& registry,
        const std::function<void(const std::string&)>& validateId =
            std::function<void(const std::string&)>(),
        const std::function<void(const std::string&,const Any&)>& publishEvent =
            std::function<void(const std::string&,const Any&)>());

    ~PluginModule();

    PluginModule(const PluginModule&) = delete;
    PluginModule& operator=(const PluginModule&) = delete;

    const std::string& id() const;
    const std::string& version() const;
    const std::string& path() const;

    std::size_t registrationCount() const;
    std::vector<std::string> registeredFunctions() const;
    NativeObjectHandle createObject(
        const std::string& relativeType,
        NativeObjectRuntime& runtime,
        const VariantList& args = VariantList());
    std::size_t objectFactoryCount() const;

    // Quiesces public entry points on the first attempt. Throws plugin_busy
    // until all retained/queued calls and plugin-owned objects have drained.
    // A successful call shuts down and unloads the library.
    void unload();
    bool unloaded() const;

private:
    static int32_t publishEventThunk(
        void* hostContext, const char* relativeEvent, const nw_value_v1* payload);

    static int32_t registerFunctionThunk(
        void* hostContext,
        const char* relativeName,
        nw_plugin_call_v1 function,
        void* userData);

    int32_t registerFunction(
        const char* relativeName,
        nw_plugin_call_v1 function,
        void* userData);
    static int32_t registerTypedFunctionThunk(
        void*, const char*, nw_plugin_call_v1, void*,
        const nw_function_signature_v1*);
    int32_t registerTypedFunction(
        const char*, nw_plugin_call_v1, void*,
        const nw_function_signature_v1*);
    int32_t registerFunctionImpl(
        const char*, nw_plugin_call_v1, void*,
        const CallableSignature&);

    static int32_t registerObjectFactoryThunk(
        void* hostContext, const char* relativeType,
        nw_plugin_create_object_v1 create,
        nw_plugin_destroy_object_v1 destroy, void* userData);
    int32_t registerObjectFactory(
        const char* relativeType,
        nw_plugin_create_object_v1 create,
        nw_plugin_destroy_object_v1 destroy, void* userData);
    static int32_t registerObjectMethodThunk(
        void* hostContext, const char* relativeType,
        const char* relativeMethod, nw_plugin_object_call_v1 function,
        void* userData);
    int32_t registerObjectMethod(
        const char* relativeType, const char* relativeMethod,
        nw_plugin_object_call_v1 function, void* userData);

    static int32_t registerObjectFactoryArgsThunk(
        void*, const char*, nw_plugin_create_object_args_v1,
        nw_plugin_destroy_object_v1, void*);
    int32_t registerObjectFactoryArgs(
        const char*, nw_plugin_create_object_args_v1,
        nw_plugin_destroy_object_v1, void*);

    struct ObjectMethod {
        nw_plugin_object_call_v1 function;
        void* userData;
    };
    struct ObjectFactory {
        nw_plugin_create_object_v1 create;
        nw_plugin_create_object_args_v1 createArgs;
        nw_plugin_destroy_object_v1 destroy;
        void* userData;
        std::map<std::string, ObjectMethod> methods;
    };
    void initialize(
        const std::function<void(const std::string&)>& validateId);

    BindingRegistry& registry_;
    std::shared_ptr<PluginEventState> eventState_;
    std::shared_ptr<PluginLibraryLease> lease_;
    std::vector<RegistrationToken> registrations_;
    std::vector<std::string> functionNames_;
    // Each weak ref identifies a registered callable whose copied snapshots
    // may remain queued/running after the registration token is released.
    std::vector<std::weak_ptr<void> > callableLifetimes_;
    mutable std::mutex mutex_;
    bool quiescing_;
    bool unloaded_;
    std::string path_;
    std::map<std::string, ObjectFactory> objectFactories_;
    std::string id_;
    std::string version_;
};

} // namespace detail
} // namespace nativeweb

#endif
