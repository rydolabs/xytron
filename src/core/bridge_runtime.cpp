#include "core/bridge_runtime.hpp"

#include "nativeweb/error.hpp"
#include "core/worker_pool.hpp"

#include <exception>

namespace nativeweb {
namespace detail {

namespace {

Any invokeBoundRequest(
    RequestId requestId,
    const std::string& method,
    const VariantList& args,
    const Callable& function)
{
    if (!function)
    {
        return makeErrorMessage(
            requestId,
            Error(
                "method_not_found",
                "NativeWeb method not found: " + method));
    }

    try
    {
        return makeResponseMessage(
            requestId,
            function.invoke(args));
    }
    catch (const Error& error)
    {
        return makeErrorMessage(
            requestId,
            error);
    }
    catch (const std::exception& error)
    {
        return makeErrorMessage(
            requestId,
            Error("native_exception", error.what()));
    }
    catch (...)
    {
        return makeErrorMessage(
            requestId,
            Error(
                "native_exception",
                "Unknown native exception"));
    }
}

} // namespace

BridgeRuntime::BridgeRuntime()
    : modules_(new ModuleRegistry()),
      asyncState_(new AsyncDispatchState())
{
}

void BridgeRuntime::bind(
    const std::string& method,
    const DynamicFunction& function)
{
    bindings_.bind(
        method,
        Callable(function));
}

void BridgeRuntime::bind(
    const std::string& method,
    const Callable& callable)
{
    bindings_.bind(
        method,
        callable);
}

RegistrationToken BridgeRuntime::registerBinding(
    const std::string& method,
    const Callable& callable)
{
    return bindings_.registerBinding(
        method,
        callable);
}

ModuleRegistration BridgeRuntime::registerModule(
    const ModuleDescriptor& descriptor)
{
    return modules_->registerModule(descriptor);
}

void BridgeRuntime::bindModuleMethod(
    const std::string& moduleId,
    const std::string& relativeMethod,
    const Callable& handler)
{
    if (relativeMethod.empty() || relativeMethod[0] == '.' ||
        relativeMethod.find("..") != std::string::npos ||
        relativeMethod.find('*') != std::string::npos ||
        relativeMethod.find("__") != std::string::npos ||
        relativeMethod.find("constructor") != std::string::npos)
        throw Error("invalid_method", "Invalid Module method name");

    for (std::size_t i = 0; i < relativeMethod.size(); ++i)
    {
        const char ch = relativeMethod[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '.'))
            throw Error("invalid_method", "Invalid Module method name");
    }
    if (!handler)
        throw Error("invalid_method", "Empty Module method handler");

    const std::uint64_t generation = modules_->generation(moduleId);
    if (!generation)
        throw Error("module_not_found", "Module is not registered: " + moduleId);

    const std::string name = moduleId + "." + relativeMethod;
    if (bindings_.has(name))
        throw Error("duplicate_method", "Method is already bound: " + name);

    const std::shared_ptr<ModuleRegistry> modules = modules_;
    bindings_.bind(name, Callable(DynamicFunction(
        [modules, moduleId, generation, handler](const VariantList& args) -> Any {
            if (modules->generation(moduleId) != generation)
                throw Error("module_unavailable",
                            "Module was unloaded or replaced: " + moduleId);
            return handler.invoke(args);
        })));
}

bool BridgeRuntime::unbind(const std::string& method)
{
    return bindings_.unbind(method);
}

bool BridgeRuntime::hasMethod(const std::string& method) const
{
    return bindings_.has(method);
}

bool BridgeRuntime::methodSignature(
    const std::string& method,
    CallableSignature* signature) const
{
    return bindings_.signature(
        method,
        signature);
}

Callable BridgeRuntime::findMethod(
    const std::string& method) const
{
    return bindings_.find(method);
}

OutboundCall BridgeRuntime::call(
    const std::string& method,
    const VariantList& args)
{
    PendingCall call =
        pending_.create();

    const Any message =
        makeRequestMessage(
            call.id,
            method,
            args);

    return OutboundCall(
        call.id,
        message,
        call.result);
}

OutboundRequest BridgeRuntime::callWithPending(
    const std::string& method,
    const VariantList& args,
    const std::shared_ptr<PendingResultBase>& result)
{
    const RequestId id =
        pending_.add(result);

    return OutboundRequest(
        id,
        makeRequestMessage(
            id,
            method,
            args));
}

EventSubscriptionId BridgeRuntime::subscribe(
    const std::string& eventName,
    const EventCallback& callback)
{
    return events_.subscribe(eventName, callback);
}

bool BridgeRuntime::unsubscribe(EventSubscriptionId id)
{
    return events_.unsubscribe(id);
}

Any BridgeRuntime::receive(const Any& message)
{
    const BridgeMessage parsed =
        parseBridgeMessage(message);

    if (parsed.type == BridgeMessageType::Response)
    {
        pending_.resolve(parsed.requestId, parsed.value);
        return Any();
    }

    if (parsed.type == BridgeMessageType::Error)
    {
        pending_.reject(
            parsed.requestId,
            Error(parsed.errorCode, parsed.errorMessage));
        return Any();
    }

    if (parsed.type == BridgeMessageType::Cancel)
    {
        cancelIncoming(parsed.requestId);
        return Any();
    }

    if (parsed.type == BridgeMessageType::Event)
    {
        events_.emit(parsed.eventName, parsed.value);
        return Any();
    }

    return invokeBoundRequest(
        parsed.requestId,
        parsed.method,
        parsed.args,
        findMethod(parsed.method));
}

void BridgeRuntime::receiveAsync(
    const Any& message,
    const BridgeResponseCallback& completion)
{
    const BridgeMessage parsed =
        parseBridgeMessage(message);

    if (parsed.type != BridgeMessageType::Request)
    {
        (void)receive(message);
        return;
    }

    const std::shared_ptr<AsyncDispatchState> state =
        asyncState_;

    if (!state || !state->active.load())
    {
        if (completion)
        {
            completion(
                makeErrorMessage(
                    parsed.requestId,
                    Error(
                        "runtime_stopped",
                        "NativeWeb runtime is shutting down")));
        }
        return;
    }

    const std::shared_ptr<std::atomic<bool> > ticket(
        new std::atomic<bool>(true));
    // A duplicate callback is arbitrary embedder code and may reenter this
    // runtime (for example, to query or cancel outstanding requests).
    // Never invoke it while holding the request-map mutex.
    bool duplicate=false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(!state->active.load()) return;
        duplicate=!state->requests.insert(
            std::make_pair(parsed.requestId,ticket)).second;
    }
    if(duplicate)
    {
        if(completion) completion(makeErrorMessage(
            parsed.requestId,Error("duplicate_request","Request ID already in flight")));
        return;
    }

    const Callable function =
        findMethod(parsed.method);

    if (!function)
    {
        cancelIncoming(parsed.requestId);
        if (completion)
        {
            completion(
                makeErrorMessage(
                    parsed.requestId,
                    Error(
                        "method_not_found",
                        "NativeWeb method not found: " +
                            parsed.method)));
        }
        return;
    }

    const bool queued =
        defaultWorkerPool().post(
            [state,
             ticket,
             completion,
             function,
             parsed]() {
                if (!state->active.load() || !ticket->load())
                    return;
                const Any response =
                    invokeBoundRequest(parsed.requestId,parsed.method,parsed.args,function);
                bool deliver=false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    std::map<RequestId,std::shared_ptr<std::atomic<bool> > >::iterator
                        it=state->requests.find(parsed.requestId);
                    if(it!=state->requests.end() && it->second==ticket)
                    {
                        state->requests.erase(it);
                        deliver=ticket->exchange(false) && state->active.load();
                    }
                }
                if(deliver && completion) completion(response);
            });

    if(!queued)
    {
        cancelIncoming(parsed.requestId);
        if(completion) completion(makeErrorMessage(
            parsed.requestId,Error("queue_overloaded",
                "NativeWeb worker queue is full or stopped")));
    }
}

Any BridgeRuntime::eventMessage(
    const std::string& eventName,
    const Any& payload) const
{
    return makeEventMessage(eventName, payload);
}

void BridgeRuntime::rejectPending(const Error& error)
{
    pending_.rejectAll(error);
}

bool BridgeRuntime::cancelIncoming(RequestId id)
{
    const std::shared_ptr<AsyncDispatchState> state=asyncState_;
    if(!state) return false;
    std::lock_guard<std::mutex> lock(state->mutex);
    std::map<RequestId,std::shared_ptr<std::atomic<bool> > >::iterator it=
        state->requests.find(id);
    if(it==state->requests.end()) return false;
    it->second->store(false);
    state->requests.erase(it);
    return true;
}

void BridgeRuntime::cancelAllIncoming()
{
    const std::shared_ptr<AsyncDispatchState> state=asyncState_;
    if(!state) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    for(std::map<RequestId,std::shared_ptr<std::atomic<bool> > >::iterator
        it=state->requests.begin();it!=state->requests.end();++it)
        it->second->store(false);
    state->requests.clear();
}

std::size_t BridgeRuntime::incomingCount() const
{
    const std::shared_ptr<AsyncDispatchState> state=asyncState_;
    if(!state) return 0;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->requests.size();
}

void BridgeRuntime::shutdown()
{
    if (asyncState_)
        asyncState_->active.store(false);
    cancelAllIncoming();

    pending_.rejectAll(
        Error(
            "webview_destroyed",
            "WebView was destroyed before the call completed"));

    events_.clear();

    modules_->clear();
    bindings_.clear();
}

std::size_t BridgeRuntime::pendingCount() const
{
    return pending_.size();
}

std::size_t BridgeRuntime::methodCount() const
{
    return bindings_.size();
}

} // namespace detail
} // namespace nativeweb
