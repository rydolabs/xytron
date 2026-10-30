#ifndef NATIVEWEB_BRIDGE_RUNTIME_HPP_INCLUDED
#define NATIVEWEB_BRIDGE_RUNTIME_HPP_INCLUDED

#include "core/binding_registry.hpp"
#include "core/module_registry.hpp"
#include "core/bridge_message.hpp"
#include "core/event_dispatcher.hpp"
#include "core/pending_call_registry.hpp"
#include "nativeweb/detail/bind.hpp"

#include <atomic>
#include <future>
#include <map>
#include <mutex>
#include <functional>
#include <memory>
#include <string>

namespace nativeweb {
namespace detail {

typedef std::function<void(const Any&)> BridgeResponseCallback;

class OutboundRequest
{
public:
    OutboundRequest(
        RequestId valueId,
        const Any& valueMessage)
        : id(valueId),
          message(valueMessage)
    {
    }

    RequestId id;
    Any message;
};

class OutboundCall
{
public:
    OutboundCall(
        RequestId valueId,
        const Any& valueMessage,
        std::future<Any>& future)
        : id(valueId),
          message(valueMessage),
          result(std::move(future))
    {
    }

    OutboundCall(OutboundCall&& other)
        : id(other.id),
          message(other.message),
          result(std::move(other.result))
    {
        other.id = 0;
    }

    OutboundCall& operator=(OutboundCall&& other)
    {
        if (this != &other)
        {
            id = other.id;
            message = other.message;
            result = std::move(other.result);
            other.id = 0;
        }
        return *this;
    }

    OutboundCall(const OutboundCall&) = delete;
    OutboundCall& operator=(const OutboundCall&) = delete;

    RequestId id;
    Any message;
    std::future<Any> result;
};

class BridgeRuntime
{
public:
    BridgeRuntime();

    void bind(
        const std::string& method,
        const DynamicFunction& function);

    void bind(
        const std::string& method,
        const Callable& callable);

    RegistrationToken registerBinding(
        const std::string& method,
        const Callable& callable);

    // First stage of language-neutral module methods. These callbacks
    // must own their executable code for the duration of in-flight calls.
    // Python/.NET process adapters and DLL/SO unload leases come later.
    ModuleRegistration registerModule(const ModuleDescriptor& descriptor);
    void bindModuleMethod(const std::string& moduleId,
                          const std::string& relativeMethod,
                          const Callable& handler);

    bool unbind(const std::string& method);
    bool hasMethod(const std::string& method) const;

    bool methodSignature(
        const std::string& method,
        CallableSignature* signature) const;

    OutboundCall call(
        const std::string& method,
        const VariantList& args);

    OutboundRequest callWithPending(
        const std::string& method,
        const VariantList& args,
        const std::shared_ptr<PendingResultBase>& result);

    EventSubscriptionId subscribe(
        const std::string& eventName,
        const EventCallback& callback);

    bool unsubscribe(EventSubscriptionId id);

    // Processes an incoming structured bridge envelope.
    // Request -> returns response/error envelope.
    // Response/Error/Event -> consumes it and returns empty Any.
    Any receive(const Any& message);

    // Incoming native requests are dispatched through the process-wide worker
    // pool. Response/Error/Event envelopes remain cheap synchronous routing.
    // Completion for a Request may execute on a worker thread.
    void receiveAsync(
        const Any& message,
        const BridgeResponseCallback& completion);

    Any eventMessage(
        const std::string& eventName,
        const Any& payload) const;

    BindingRegistry& bindingRegistry() { return bindings_; }

    // Cancel all outstanding native-to-JS calls without shutting down
    // bindings, subscriptions or future calls (for document navigation).
    void rejectPending(const Error& error);
    bool cancelIncoming(RequestId requestId);
    void cancelAllIncoming();
    std::size_t incomingCount() const;
    void shutdown();

    std::size_t pendingCount() const;
    std::size_t methodCount() const;

private:
    struct AsyncDispatchState
    {
        AsyncDispatchState()
            : active(true)
        {
        }

        std::atomic<bool> active;
        std::mutex mutex;
        std::map<RequestId,std::shared_ptr<std::atomic<bool> > > requests;
    };

    Callable findMethod(const std::string& method) const;

    BindingRegistry bindings_;
    std::shared_ptr<ModuleRegistry> modules_;
    PendingCallRegistry pending_;
    EventDispatcher events_;
    std::shared_ptr<AsyncDispatchState> asyncState_;
};

} // namespace detail
} // namespace nativeweb

#endif
