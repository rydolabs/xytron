#include "plugin/plugin_module.hpp"

#include "nativeweb/error.hpp"
#include "nativeweb/plugin_abi.h"
#include "plugin/shared_library.hpp"

#include <atomic>
#include <cstring>
#include <cstddef>
#include <limits>
#include <memory>
#include <sstream>

namespace nativeweb {
namespace detail {

struct PluginEventState {
    explicit PluginEventState(
        const std::function<void(const std::string&,const Any&)>& callback)
        : accepting(true), deliver(callback) {}
    std::mutex mutex;
    bool accepting;
    std::string id;
    std::function<void(const std::string&,const Any&)> deliver;
};

namespace {

// A plugin may publish events only synchronously while one of its
// registered C ABI functions is executing on this thread. This prevents
// asynchronous publisher use after a module has been quiesced/unloaded.
thread_local PluginModule* activePublisherModule = 0;
thread_local std::shared_ptr<PluginEventState> activePublisherState;
struct PluginPublishScope {
    PluginPublishScope(PluginModule* module,
                       const std::shared_ptr<PluginEventState>& state)
        : previous(activePublisherModule), previousState(activePublisherState) {
        activePublisherModule = module;
        activePublisherState = state;
    }
    ~PluginPublishScope() {
        activePublisherModule = previous;
        activePublisherState = previousState;
    }
    PluginModule* previous;
    std::shared_ptr<PluginEventState> previousState;
};

std::string copyString(
    const nw_string_view_v1& value)
{
    if (!value.data || value.size == 0)
        return std::string();

    return std::string(
        value.data,
        value.data +
            static_cast<std::size_t>(
                value.size));
}

nw_value_v1 anyToPluginValue(
    const Any& value,
    std::string* stringStorage,
    Binary* binaryStorage)
{
    nw_value_v1 output;
    std::memset(
        &output,
        0,
        sizeof(output));

    output.struct_size =
        static_cast<uint32_t>(
            sizeof(output));

    if (value.empty())
    {
        output.type =
            NW_VALUE_NULL_V1;
        return output;
    }

    if (value.type() == typeid(bool))
    {
        output.type =
            NW_VALUE_BOOL_V1;

        output.data.boolean_value =
            AnyCast<bool>(value)
                ? 1u
                : 0u;

        return output;
    }

    if (value.type() == typeid(int))
    {
        output.type =
            NW_VALUE_INT32_V1;

        output.data.int32_value =
            static_cast<int32_t>(
                AnyCast<int>(value));

        return output;
    }

    if (value.type() == typeid(double))
    {
        output.type =
            NW_VALUE_DOUBLE_V1;

        output.data.double_value =
            AnyCast<double>(value);

        return output;
    }

    if (value.type() == typeid(std::string))
    {
        *stringStorage =
            AnyCast<std::string>(value);

        output.type =
            NW_VALUE_STRING_V1;

        output.data.string_value.data =
            stringStorage->data();

        output.data.string_value.size =
            static_cast<uint64_t>(
                stringStorage->size());

        return output;
    }

    if (value.type() == typeid(Binary))
    {
        *binaryStorage =
            AnyCast<Binary>(value);

        output.type =
            NW_VALUE_BINARY_V1;

        output.data.binary_value.data =
            binaryStorage->empty()
                ? 0
                : &(*binaryStorage)[0];

        output.data.binary_value.size =
            static_cast<uint64_t>(
                binaryStorage->size());

        return output;
    }

    throw Error(
        "plugin_value_unsupported",
        "NativeWeb plugin ABI v1 does not support this argument type yet");
}

Any pluginValueToAny(
    const nw_value_v1& value)
{
    if (
        value.struct_size <
        sizeof(nw_value_v1))
    {
        throw Error(
            "plugin_invalid_value",
            "Plugin returned an invalid nw_value_v1 structure");
    }

    switch (value.type)
    {
    case NW_VALUE_NULL_V1:
        return Any();

    case NW_VALUE_BOOL_V1:
        return Any(
            value.data.boolean_value != 0);

    case NW_VALUE_INT32_V1:
        return Any(
            static_cast<int>(
                value.data.int32_value));

    case NW_VALUE_DOUBLE_V1:
        return Any(
            value.data.double_value);

    case NW_VALUE_STRING_V1:
        return Any(
            copyString(
                value.data.string_value));

    case NW_VALUE_BINARY_V1:
    {
        Binary bytes;

        if (
            value.data.binary_value.data &&
            value.data.binary_value.size)
        {
            const uint64_t size =
                value.data.binary_value.size;

            if (
                size >
                static_cast<uint64_t>(
                    std::numeric_limits<std::size_t>::max()))
            {
                throw Error(
                    "plugin_invalid_value",
                    "Plugin binary result is too large for this process");
            }

            bytes.assign(
                value.data.binary_value.data,
                value.data.binary_value.data +
                    static_cast<std::size_t>(size));
        }

        return Any(bytes);
    }

    default:
        throw Error(
            "plugin_invalid_value",
            "Plugin returned an unknown value type");
    }
}

template <typename Invoker>
Any invokePluginCallback(const VariantList& args, const Invoker& invoke)
{
            std::vector<nw_value_v1> pluginArgs(
                args.size());

            std::vector<std::string> strings(
                args.size());

            std::vector<Binary> binaries(
                args.size());

            for (
                std::size_t i = 0;
                i < args.size();
                ++i)
            {
                pluginArgs[i] =
                    anyToPluginValue(
                        args[i],
                        &strings[i],
                        &binaries[i]);
            }

            nw_value_v1 result;
            std::memset(
                &result,
                0,
                sizeof(result));
            result.struct_size =
                static_cast<uint32_t>(
                    sizeof(result));
            result.type =
                NW_VALUE_NULL_V1;

            nw_error_v1 error;
            std::memset(
                &error,
                0,
                sizeof(error));
            error.struct_size =
                static_cast<uint32_t>(
                    sizeof(error));

            const int32_t status =
                invoke(
                    pluginArgs.empty() ? 0 : &pluginArgs[0],
                    static_cast<uint64_t>(pluginArgs.size()),
                    &result, &error);

            if (status != 0)
            {
                std::string code =
                    copyString(error.code);

                std::string message =
                    copyString(error.message);

                if (code.empty())
                    code = "plugin_call_failed";

                if (message.empty())
                    message = "Plugin call failed";

                throw Error(
                    code,
                    message);
            }

            return pluginValueToAny(result);
}

Callable makePluginCallable(
    nw_plugin_call_v1 function,
    void* userData,
    const std::shared_ptr<void>& lifetime,
    const CallableSignature& signature,
    PluginModule* module, const std::shared_ptr<PluginEventState>& state)
{
    // Preserve the lease even when a consumer extracts/copies dynamicFunction()
    // instead of copying the full Callable wrapper.
    const DynamicFunction dynamic = [function, userData, lifetime, signature, module, state](
        const VariantList& args) -> Any {
        (void)lifetime;
        PluginPublishScope scopedEventPublisher(module,state);
        if (signature.result.kind != CallableTypeKind::Unknown)
        {
            if (args.size() != signature.arity())
                throw Error("invalid_arguments", "Plugin argument count mismatch");
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                const CallableTypeKind kind = signature.arguments[i].kind;
                const std::type_info& type = args[i].type();
                const bool valid =
                    (kind == CallableTypeKind::Boolean && type == typeid(bool)) ||
                    (kind == CallableTypeKind::Integer && type == typeid(int)) ||
                    (kind == CallableTypeKind::Floating && type == typeid(double)) ||
                    (kind == CallableTypeKind::String && type == typeid(std::string)) ||
                    (kind == CallableTypeKind::Binary && type == typeid(Binary));
                if (!valid)
                    throw Error("invalid_arguments", "Plugin argument type mismatch");
            }
        }
        return invokePluginCallback(args,
            [function, userData](const nw_value_v1* values, uint64_t count,
                nw_value_v1* result, nw_error_v1* error) -> int32_t {
                return function(userData, values, count, result, error);
            });
    };
    return Callable(dynamic, signature, lifetime);
}

ManagedObjectMethod makePluginObjectMethod(
    nw_plugin_object_call_v1 function, void* userData)
{
    return [function, userData](const std::shared_ptr<void>& object,
        const VariantList& args) -> Any {
        return invokePluginCallback(args,
            [function, userData, &object](const nw_value_v1* values,
                uint64_t count, nw_value_v1* result,
                nw_error_v1* error) -> int32_t {
                return function(userData, object.get(), values, count,
                    result, error);
            });
    };
}

bool validPluginId(
    const std::string& id)
{
    if (id.empty())
        return false;

    for (
        std::size_t i = 0;
        i < id.size();
        ++i)
    {
        const char ch = id[i];

        const bool ok =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' ||
            ch == '-';

        if (!ok)
            return false;
    }

    return true;
}

} // namespace

class PluginLibraryLease
{
public:
    explicit PluginLibraryLease(
        const std::string& path)
        : liveObjects(0), library(path)
    {
        std::memset(
            &api,
            0,
            sizeof(api));

        api.struct_size =
            static_cast<uint32_t>(
                sizeof(api));
    }

    ~PluginLibraryLease()
    {
        if (api.shutdown)
            api.shutdown(
                api.plugin_context);
    }

    std::atomic<std::size_t> liveObjects;
    SharedLibrary library;
    nw_plugin_api_v1 api;
};

PluginModule::PluginModule(
    const std::string& path,
    BindingRegistry& registry,
    const std::function<void(const std::string&)>& validateId,
    const std::function<void(const std::string&,const Any&)>& publishEvent)
    : registry_(registry),
      eventState_(new PluginEventState(publishEvent)),
      lease_(new PluginLibraryLease(path)),
      quiescing_(false), unloaded_(false), path_(path)
{
    initialize(validateId);
}

PluginModule::~PluginModule()
{
    // Tokens unregister public entry points first. Callable copies already
    // queued/running retain PluginLibraryLease and therefore the DLL/SO.
    std::lock_guard<std::mutex> lock(mutex_);
    quiescing_ = true;
    {
        std::lock_guard<std::mutex> eventLock(eventState_->mutex);
        eventState_->accepting = false;
    }
    registrations_.clear();
    lease_.reset();
}

const std::string& PluginModule::id() const
{
    return id_;
}

const std::string& PluginModule::version() const
{
    return version_;
}

const std::string& PluginModule::path() const
{
    return path_;
}

std::size_t PluginModule::registrationCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return registrations_.size();
}

std::vector<std::string> PluginModule::registeredFunctions() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return functionNames_;
}

std::size_t PluginModule::objectFactoryCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return objectFactories_.size();
}

NativeObjectHandle PluginModule::createObject(
    const std::string& relativeType,
    NativeObjectRuntime& runtime, const VariantList& args)
{
    ObjectFactory factory;
    std::shared_ptr<PluginLibraryLease> lease;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (quiescing_ || unloaded_)
            throw Error("plugin_unloading", "Plugin is quiescing or unloaded");

        const std::map<std::string, ObjectFactory>::const_iterator found =
            objectFactories_.find(relativeType);
        if (found == objectFactories_.end())
            throw Error("plugin_object_factory_not_found", "Plugin object factory not found");

        factory = found->second;
        lease = lease_;
        // Count construction too: unload cannot race with a factory callback.
        lease->liveObjects.fetch_add(1);
    }

    void* raw = 0;
    try {
        if (factory.createArgs) {
            invokePluginCallback(args,
                [&](const nw_value_v1* values, uint64_t count,
                    nw_value_v1*, nw_error_v1* error) -> int32_t {
                    raw = factory.createArgs(factory.userData, values, count, error);
                    return raw ? 0 : -1;
                });
        } else {
            if (!args.empty())
                throw Error("plugin_factory_arguments_unsupported",
                            "This plugin factory does not accept arguments");
            raw = factory.create(factory.userData);
        }
    } catch (...) {
        lease->liveObjects.fetch_sub(1);
        throw;
    }
    if (!raw) {
        lease->liveObjects.fetch_sub(1);
        throw Error("plugin_object_create_failed", "Plugin factory returned null");
    }

    // Destroy while the module lease is still alive, then decrement.
    const std::shared_ptr<void> object(raw,
        [factory, lease](void* ptr) {
            factory.destroy(factory.userData, ptr);
            lease->liveObjects.fetch_sub(1);
        });
    const NativeObjectHandle handle =
        runtime.add(object, id_ + "." + relativeType, lease);
    try {
        for (std::map<std::string, ObjectMethod>::const_iterator it =
                 factory.methods.begin(); it != factory.methods.end(); ++it) {
            runtime.bindManagedMethod(handle, it->first,
                makePluginObjectMethod(it->second.function, it->second.userData));
        }
    } catch (...) {
        runtime.release(handle);
        throw;
    }
    return handle;
}

void PluginModule::unload()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (unloaded_)
        return;

    // No later lookups may acquire fresh callables from our registrations.
    quiescing_ = true;
    {
        std::lock_guard<std::mutex> eventLock(eventState_->mutex);
        eventState_->accepting = false;
    }
    registrations_.clear();

    // Existing callables (including worker-queued snapshots) and objects
    // still own library references; never force-unload code they can invoke.
    for (std::size_t i = 0; i < callableLifetimes_.size(); ++i) {
        if (!callableLifetimes_[i].expired())
            throw Error("plugin_busy", "Plugin has retained or pending calls");
    }
    if (lease_ && lease_->liveObjects.load() != 0)
        throw Error("plugin_busy", "Plugin has live native objects or object calls");

    objectFactories_.clear();
    lease_.reset();
    unloaded_ = true;
}

bool PluginModule::unloaded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return unloaded_;
}

int32_t PluginModule::registerObjectFactoryThunk(
    void* hostContext, const char* relativeType,
    nw_plugin_create_object_v1 create,
    nw_plugin_destroy_object_v1 destroy, void* userData)
{
    if (!hostContext) return -1;
    try {
        return static_cast<PluginModule*>(hostContext)->registerObjectFactory(
            relativeType, create, destroy, userData);
    } catch (...) {
        return -1;
    }
}

int32_t PluginModule::registerObjectFactory(
    const char* relativeType,
    nw_plugin_create_object_v1 create,
    nw_plugin_destroy_object_v1 destroy, void* userData)
{
    if (!relativeType || !*relativeType || !create || !destroy)
        return -1;
    const std::string name(relativeType);
    if (!validPluginId(name) || objectFactories_.count(name))
        return -1;
    ObjectFactory factory;
    factory.create = create;
    factory.createArgs = 0;
    factory.destroy = destroy;
    factory.userData = userData;
    objectFactories_.insert(std::make_pair(name, factory));
    return 0;
}

int32_t PluginModule::registerObjectFactoryArgsThunk(
    void* ctx, const char* name, nw_plugin_create_object_args_v1 create,
    nw_plugin_destroy_object_v1 destroy, void* data)
{
    if (!ctx) return -1;
    try { return static_cast<PluginModule*>(ctx)->registerObjectFactoryArgs(
        name, create, destroy, data); }
    catch (...) { return -1; }
}

int32_t PluginModule::registerObjectFactoryArgs(
    const char* relativeType, nw_plugin_create_object_args_v1 create,
    nw_plugin_destroy_object_v1 destroy, void* userData)
{
    if (!relativeType || !*relativeType || !create || !destroy) return -1;
    const std::string name(relativeType);
    if (!validPluginId(name) || objectFactories_.count(name)) return -1;
    ObjectFactory factory = {};
    factory.create = 0;
    factory.createArgs = create;
    factory.destroy = destroy;
    factory.userData = userData;
    objectFactories_.insert(std::make_pair(name, factory));
    return 0;
}

int32_t PluginModule::registerObjectMethodThunk(
    void* hostContext, const char* relativeType,
    const char* relativeMethod, nw_plugin_object_call_v1 function,
    void* userData)
{
    if (!hostContext) return -1;
    try {
        return static_cast<PluginModule*>(hostContext)->registerObjectMethod(
            relativeType, relativeMethod, function, userData);
    } catch (...) {
        return -1;
    }
}

int32_t PluginModule::registerObjectMethod(
    const char* relativeType, const char* relativeMethod,
    nw_plugin_object_call_v1 function, void* userData)
{
    if (!relativeType || !relativeMethod || !function) return -1;
    const std::string type(relativeType), method(relativeMethod);
    if (!validPluginId(type) || !validPluginId(method)) return -1;
    std::map<std::string, ObjectFactory>::iterator found =
        objectFactories_.find(type);
    if (found == objectFactories_.end() || found->second.methods.count(method))
        return -1;
    ObjectMethod entry = { function, userData };
    found->second.methods.insert(std::make_pair(method, entry));
    return 0;
}

void PluginModule::initialize(
    const std::function<void(const std::string&)>& validateId)
{
    void* symbol =
        lease_->library.symbol(
            NATIVEWEB_PLUGIN_INIT_SYMBOL_V1);

    nw_plugin_init_v1 init =
        reinterpret_cast<nw_plugin_init_v1>(
            symbol);

    const int32_t initStatus =
        init(&lease_->api);

    if (initStatus != 0)
    {
        throw Error(
            "plugin_init_failed",
            "Plugin initialization failed");
    }

    if (
        lease_->api.struct_size <
        sizeof(nw_plugin_api_v1))
    {
        throw Error(
            "plugin_invalid_descriptor",
            "Plugin returned an invalid ABI descriptor");
    }

    if (
        lease_->api.abi_version !=
        NATIVEWEB_PLUGIN_ABI_V1)
    {
        throw Error(
            "plugin_abi_mismatch",
            "Plugin ABI version is not supported");
    }

    if (
        !lease_->api.id ||
        !validPluginId(
            lease_->api.id))
    {
        throw Error(
            "plugin_invalid_id",
            "Plugin id is empty or contains unsupported characters");
    }

    if (!lease_->api.register_api)
    {
        throw Error(
            "plugin_invalid_descriptor",
            "Plugin register_api callback is missing");
    }

    id_ = lease_->api.id;
    eventState_->id = id_;
    version_ =
        lease_->api.version
            ? lease_->api.version
            : std::string();

    // The descriptor must be read first to learn the ID. Reject conflicts
    // before invoking register_api, which may otherwise overwrite bindings.
    if (validateId)
        validateId(id_);

    nw_host_api_v1 host;
    std::memset(
        &host,
        0,
        sizeof(host));

    host.struct_size =
        static_cast<uint32_t>(
            sizeof(host));

    host.abi_version =
        NATIVEWEB_PLUGIN_ABI_V1;

    host.host_context = this;
    host.register_function =
        &PluginModule::registerFunctionThunk;
    host.register_object_factory =
        &PluginModule::registerObjectFactoryThunk;
    host.register_object_method =
        &PluginModule::registerObjectMethodThunk;
    host.register_object_factory_args =
        &PluginModule::registerObjectFactoryArgsThunk;
    host.register_typed_function =
        &PluginModule::registerTypedFunctionThunk;
    host.publish_event =
        &PluginModule::publishEventThunk;

    const int32_t registerStatus =
        lease_->api.register_api(
            lease_->api.plugin_context,
            &host);

    if (registerStatus != 0)
    {
        throw Error(
            "plugin_register_failed",
            "Plugin API registration failed");
    }
}

int32_t PluginModule::publishEventThunk(
    void* context, const char* event, const nw_value_v1* payload)
{
    // Compare the opaque pointer only. The PluginModule object may already
    // be destroyed while a copied callable still owns the DLL/SO lease.
    // The shared event state is retained by that callable and its TLS scope.
    if (!context || activePublisherModule != static_cast<PluginModule*>(context) ||
        !activePublisherState)
        return -1;
    try {
        if (!event || !payload || !validPluginId(event) ||
            payload->struct_size < sizeof(nw_value_v1) ||
            payload->type != NW_VALUE_STRING_V1 ||
            payload->data.string_value.size > 4096 ||
            (payload->data.string_value.size && !payload->data.string_value.data))
            return -1;
        const std::shared_ptr<PluginEventState> state=activePublisherState;
        std::function<void(const std::string&,const Any&)> delivery;
        std::string channel;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->accepting || !state->deliver)
                return -1;
            channel=state->id+"."+event;
            delivery=state->deliver;
        }
        delivery(channel,pluginValueToAny(*payload));
        return 0;
    } catch (...) {
        return -1; // No exceptions cross the C ABI.
    }
}

int32_t PluginModule::registerFunctionThunk(
    void* hostContext,
    const char* relativeName,
    nw_plugin_call_v1 function,
    void* userData)
{
    if (!hostContext)
        return -1;

    try
    {
        return static_cast<PluginModule*>(
            hostContext)->registerFunction(
                relativeName,
                function,
                userData);
    }
    catch (...)
    {
        return -1;
    }
}

int32_t PluginModule::registerFunction(
    const char* relativeName,
    nw_plugin_call_v1 function,
    void* userData)
{
    return registerFunctionImpl(relativeName, function, userData,
                                CallableSignature());
}

int32_t PluginModule::registerTypedFunctionThunk(
    void* context, const char* relative, nw_plugin_call_v1 fn,
    void* userData, const nw_function_signature_v1* sig)
{
    if (!context) return -1;
    try {
        return static_cast<PluginModule*>(context)->registerTypedFunction(
            relative, fn, userData, sig);
    } catch (...) { return -1; }
}

int32_t PluginModule::registerTypedFunction(
    const char* relative, nw_plugin_call_v1 fn, void* userData,
    const nw_function_signature_v1* sig)
{
    if (!sig || sig->struct_size < sizeof(nw_function_signature_v1) ||
        sig->argument_count > 64 ||
        (sig->argument_count && !sig->argument_types)) return -1;
    CallableSignature descriptor;
    const auto decode = [](uint32_t type, bool result) -> CallableType {
        switch (type) {
        case NW_VALUE_NULL_V1:
            if (result) return CallableType(CallableTypeKind::Void, "void");
            break;
        case NW_VALUE_BOOL_V1:
            return CallableType(CallableTypeKind::Boolean, "boolean");
        case NW_VALUE_INT32_V1:
            return CallableType(CallableTypeKind::Integer, "integer");
        case NW_VALUE_DOUBLE_V1:
            return CallableType(CallableTypeKind::Floating, "number");
        case NW_VALUE_STRING_V1:
            return CallableType(CallableTypeKind::String, "string");
        case NW_VALUE_BINARY_V1:
            return CallableType(CallableTypeKind::Binary, "binary");
        default: break;
        }
        throw Error("plugin_invalid_signature", "Invalid ABI signature type");
    };
    descriptor.result = decode(sig->result_type, true);
    for (uint32_t i = 0; i < sig->argument_count; ++i)
        descriptor.arguments.push_back(decode(sig->argument_types[i], false));
    return registerFunctionImpl(relative, fn, userData, descriptor);
}

int32_t PluginModule::registerFunctionImpl(
    const char* relativeName,
    nw_plugin_call_v1 function,
    void* userData,
    const CallableSignature& signature)
{
    if (
        !relativeName ||
        !*relativeName ||
        !function)
    {
        return -1;
    }

    const std::string relative(
        relativeName);

    if (
        relative.find('.') !=
        std::string::npos)
    {
        return -1;
    }

    const std::string method =
        id_ + "." + relative;

    if (registry_.has(method))
        return -1;

    // A distinct call lease per method permits exact post-unregister busy
    // checks without confusing ordinary registration owners with call copies.
    const std::shared_ptr<void> lifetime(
        new std::shared_ptr<PluginLibraryLease>(lease_));

    RegistrationToken token =
        registry_.registerBinding(
            method,
            makePluginCallable(
                function,
                userData,
                lifetime, signature, this, eventState_));

    registrations_.push_back(
        std::move(token));
    callableLifetimes_.push_back(lifetime);
    functionNames_.push_back(method);

    return 0;
}

} // namespace detail
} // namespace nativeweb
