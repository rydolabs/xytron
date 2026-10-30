#include "nativeweb/webview.hpp"
#include "nativeweb/app_resources.hpp"

#include "browser/common/backend_registry.hpp"
#include "core/bridge_runtime.hpp"
#include "core/native_object_runtime.hpp"
#include "core/process_broker.hpp"
#include "core/native_permission_policy.hpp"
#include "nativeweb/error.hpp"
#include "plugin/plugin_module.hpp"

#include <map>
#include <atomic>
#include <set>
#include <thread>

#include <cstdint>
#include <mutex>
#include <sstream>
#include <utility>

namespace nativeweb {

namespace {
bool validManagedId(const std::string& s)
{
    if(s.empty() || s.size()>128 || s.find("__")!=std::string::npos)
        return false;
    const std::string forbidden[]={"constructor","prototype","then","toString",
        "valueOf","hasOwnProperty","__proto__","create","dispose"};
    for(size_t i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);++i)
        if(s==forbidden[i])return false;
    for(size_t i=0;i<s.size();++i){
        const char c=s[i];
        if(i==0 && !((c>='A' && c<='Z')||(c>='a' && c<='z')))
            return false;
        if(!((c>='A' && c<='Z')||(c>='a' && c<='z')||
             (c>='0' && c<='9')||c=='_'))return false;
    }
    return true;
}
bool validManagedOperation(const std::string& plugin,const std::string& op)
{
    if(op.size()>256 || op.compare(0,plugin.size()+1,plugin+".")!=0)
        return false;
    const std::string name=op.substr(plugin.size()+1);
    const std::size_t d1=name.find('.');
    if(d1==std::string::npos)return false;
    const std::string cls=name.substr(0,d1);
    if(!validManagedId(cls))return false;
    const std::string tail=name.substr(d1+1);
    if(tail=="create" || tail=="dispose")return true;
    const std::size_t d2=tail.find('.');
    if(d2==std::string::npos)return false;
    const std::string verb=tail.substr(0,d2);
    return (verb=="method" || verb=="get" || verb=="set") &&
         validManagedId(tail.substr(d2+1));
}
}
class WebView::Impl :
    public detail::BrowserBackendListener
{
private:
    struct LoadedPlugin
    {
        std::shared_ptr<detail::PluginModule> module;
        std::vector<detail::RegistrationToken> factories;
    };
    // Queued bridge callbacks must own their object state independently of
    // WebView::Impl. In particular, they must never capture a raw Impl*.
    struct ObjectBridgeState
    {
        ObjectBridgeState() : accepting(true) {}

        NativeObjectHandle create(
            const std::shared_ptr<detail::PluginModule>& module,
            const std::string& relativeType,
            const VariantList& args)
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex);
            if (!accepting)
                throw Error("webview_destroyed", "WebView is shutting down");
            return module->createObject(relativeType, objects, args);
        }

        void stop()
        {
            std::lock_guard<std::mutex> lock(lifecycleMutex);
            accepting = false;
            objects.clear();
        }

        std::mutex lifecycleMutex;
        bool accepting;
        detail::NativeObjectRuntime objects;
    };

    struct ManagedWorkerState
    {
        ManagedWorkerState(const std::string& id,
            const std::shared_ptr<detail::ProcessBroker>& p)
            : pluginId(id), broker(p), accepting(true) {}
        std::string pluginId;
        detail::NativePermissionPolicy permissions;
        std::shared_ptr<detail::ProcessBroker> broker;
        std::atomic<bool> accepting;
        std::vector<detail::RegistrationToken> registrations;
        std::vector<std::string> names;
    };

    struct ResponseSinkState
    {
        explicit ResponseSinkState(Impl* valueOwner)
            : owner(valueOwner)
        {
        }

        std::mutex mutex;
        Impl* owner;
        std::uint64_t generation = 0;
    };

public:
    Impl()
        : objects_(new ObjectBridgeState()),
          responseSink_(new ResponseSinkState(this)),
          listener_(0),
          resolvedEngine_(Engine::Auto)
    {
        // Copy shared ownership into every bridge callback: WorkerPool tasks
        // can outlive the WebView and its BridgeRuntime.
        const std::shared_ptr<ObjectBridgeState> objectState = objects_;
        runtime_.bind(
            "__native_object.call",
            DynamicFunction(
                [objectState](const VariantList& args) -> Any
                {
                    if (args.size() < 3)
                    {
                        throw Error(
                            "invalid_native_object_call",
                            "Native object call requires id, type and method");
                    }

                    const std::string idText =
                        AnyCast<std::string>(args[0]);

                    const std::string typeName =
                        AnyCast<std::string>(args[1]);

                    const std::string method =
                        AnyCast<std::string>(args[2]);

                    NativeObjectHandle handle =
                        parseObjectHandle(
                            idText,
                            typeName);

                    VariantList methodArgs;

                    const std::size_t firstArg = 3;

                    for (std::size_t i = firstArg;
                         i < args.size();
                         ++i)
                    {
                        methodArgs.push_back(args[i]);
                    }

                    return objectState->objects.call(
                        handle,
                        method,
                        methodArgs);
                }));

        runtime_.bind(
            "__native_object.release",
            DynamicFunction(
                [objectState](const VariantList& args) -> Any
                {
                    if (args.empty())
                    {
                        throw Error(
                            "invalid_native_object_release",
                            "Native object release requires an id");
                    }

                    const std::string idText =
                        AnyCast<std::string>(args[0]);

                    const std::string typeName =
                        args.size() >= 2
                            ? AnyCast<std::string>(args[1])
                            : std::string();

                    return Any(
                        objectState->objects.release(
                            parseObjectHandle(
                                idText,
                                typeName)));
                }));
    }

    ~Impl()
    {
        shutdown();
    }

    void create(
        NativeWindowHandle parent,
        const std::string& source,
        const WebViewOptions& options)
    {
        if (backend_)
            throw Error(
                "webview_already_created",
                "NativeWeb WebView is already created");

        if (source.compare(0, 6, "app://") == 0 &&
            (!options.appResources ||
             !options.appResources->contains(source)))
            throw Error("app_resource_not_found",
                "app:// entry must be present in its packaged resource bundle");

        packagedEntry_.clear();
        if(AppResourceBundle::isCanonicalUrl(source) && options.appResources &&
           options.appResources->contains(source))packagedEntry_=source;

        backend_ =
            detail::createBrowserBackend(
                options.engine,
                &resolvedEngine_);

        backend_->setListener(this);

        detail::BrowserCreateParams params;
        params.parent = parent;
        params.source = source;
        // Freeze the host-supplied bundle for the lifetime of this browser;
        // CEF IO and WebView2 resource threads must never race a mutable
        // developer-owned AppResourceBundle.
        if (options.appResources)
            params.appResources.reset(
                new AppResourceBundle(*options.appResources));

        try
        {
            backend_->create(params);
        }
        catch (...)
        {
            backend_->setListener(0);
            backend_.reset();
            resolvedEngine_ = Engine::Auto;
            throw;
        }
    }

    void destroy()
    {
        revokeManagedWorkers();
        invalidateDocument();
        detachResponseSink();
        objects_->stop();
        runtime_.shutdown();

        if (backend_)
            backend_->destroy();
    }

    void shutdown()
    {
        revokeManagedWorkers();
        invalidateDocument();
        detachResponseSink();
        objects_->stop();
        runtime_.shutdown();

        if (backend_)
        {
            backend_->setListener(0);

            if (backend_->isCreated())
                backend_->destroy();

            backend_.reset();
        }

        resolvedEngine_ = Engine::Auto;
    }

    void setListener(WebViewListener* listener)
    {
        listener_ = listener;
    }

    bool isCreated() const
    {
        return backend_ && backend_->isCreated();
    }

    Engine engine() const
    {
        return resolvedEngine_;
    }

    Capabilities capabilities() const
    {
        if (!backend_)
            return Capabilities();

        return backend_->capabilities();
    }

    void load(const std::string& source)
    {
        revokeManagedWorkers();
        invalidateDocument();
        requireBackend()->load(source);
    }

    void reload()
    {
        revokeManagedWorkers();
        invalidateDocument();
        requireBackend()->reload();
    }

    std::string loadPlugin(const std::string& path)
    {
        std::lock_guard<std::mutex> guard(pluginsMutex_);
        const std::shared_ptr<ResponseSinkState> sink=responseSink_;
        std::shared_ptr<detail::PluginModule> module(
            new detail::PluginModule(path, runtime_.bindingRegistry(),
                [this](const std::string& candidate) {
                    if (plugins_.count(candidate))
                        throw Error("plugin_duplicate",
                                    "Plugin is already loaded: " + candidate);
                },
                [sink](const std::string& event,const Any& payload) {
                    // Worker calls may outlive WebView; use the shared sink.
                    std::lock_guard<std::mutex> guard(sink->mutex);
                    if (!sink->owner)
                        throw Error("webview_destroyed",
                                    "Plugin event arrived after WebView shutdown");
                    sink->owner->emit(event,payload);
                }));
        const std::string pluginId = module->id();
        plugins_[pluginId].module = module;
        return pluginId;
    }

    void bindPluginFactory(const std::string& method,
                           const std::string& pluginId,
                           const std::string& relativeType)
    {
        std::lock_guard<std::mutex> guard(pluginsMutex_);
        if (method.empty() || method.find("__native_object.") == 0)
            throw Error("reserved_method", "Invalid plugin factory binding name");
        std::map<std::string, LoadedPlugin>::iterator it = plugins_.find(pluginId);
        if (it == plugins_.end())
            throw Error("plugin_not_found", "Plugin is not loaded: " + pluginId);
        std::weak_ptr<detail::PluginModule> module(it->second.module);
        const std::shared_ptr<ObjectBridgeState> objectState = objects_;
        detail::RegistrationToken token = runtime_.registerBinding(method,
            detail::Callable(DynamicFunction(
                [objectState, module, relativeType](const VariantList& args) -> Any {
                    std::shared_ptr<detail::PluginModule> active = module.lock();
                    if (!active)
                        throw Error("plugin_unloading", "Plugin has been unloaded");
                    return Any(objectState->create(active, relativeType, args));
                })));
        it->second.factories.push_back(std::move(token));
    }

    NativeObjectHandle createPluginObject(const std::string& pluginId,
                                          const std::string& relativeType,
                                          const VariantList& args)
    {
        std::lock_guard<std::mutex> guard(pluginsMutex_);
        std::map<std::string, LoadedPlugin>::iterator it = plugins_.find(pluginId);
        if (it == plugins_.end())
            throw Error("plugin_not_found", "Plugin is not loaded: " + pluginId);
        return objects_->create(it->second.module, relativeType, args);
    }

    void unloadPlugin(const std::string& pluginId)
    {
        std::lock_guard<std::mutex> guard(pluginsMutex_);
        std::map<std::string, LoadedPlugin>::iterator it = plugins_.find(pluginId);
        if (it == plugins_.end())
            throw Error("plugin_not_found", "Plugin is not loaded: " + pluginId);
        it->second.factories.clear();
        it->second.module->unload();
        plugins_.erase(it);
    }

    void bindManagedWorker(const std::string& plugin,
        const std::vector<std::string>& operations,
        const std::vector<std::string>& command,unsigned timeoutMs)
    {
        if(!isCreated() || packagedEntry_.empty())
            throw Error("permission_denied","Managed workers require packaged app://");
        if(!validManagedId(plugin) || operations.empty() || operations.size()>128)
            throw Error("invalid_grant","Invalid managed worker grant");
        std::set<std::string> unique;
        for(size_t i=0;i<operations.size();++i){
            if(operations[i].size()>200 || !validManagedOperation(plugin,operations[i]) ||
               !unique.insert(operations[i]).second || runtime_.hasMethod(operations[i]))
                throw Error("invalid_grant","Invalid or duplicate managed operation");
        }
        if(!trustedFrame_.engineVerified || !trustedFrame_.mainFrame ||
           !trustedFrame_.trustedContent || !trustedFrame_.frameId ||
           !trustedFrame_.navigation)
            throw Error("permission_denied","No verified trusted document");
        std::shared_ptr<detail::ProcessBroker> broker(new detail::ProcessBroker(command,timeoutMs));
        std::shared_ptr<ManagedWorkerState> state(new ManagedWorkerState(plugin,broker));
        state->permissions.adoptTrustedMainFrame(
            trustedFrame_.frameId,trustedFrame_.navigation);
        for(size_t i=0;i<operations.size();++i){
            const std::string op=operations[i];
            if (!state->permissions.grantExactMethod(op))
                throw Error("invalid_grant","Managed permission grant rejected");
            state->registrations.push_back(runtime_.registerBinding(op,
                detail::Callable(DynamicFunction([state,op](const VariantList& args)->Any{
                    if(!state->accepting.load())
                        throw Error("managed_session_revoked","Managed session revoked");
                    if(!state->permissions.isMethodGranted(op))
                        throw Error("permission_denied","Managed operation not granted");
                    const Any value=state->broker->invoke(op,args);
                    if(!state->accepting.load())
                        throw Error("managed_session_revoked","Managed session revoked");
                    if(!state->permissions.isMethodGranted(op))
                        throw Error("permission_denied","Managed permission revoked");
                    return value;
                }))));
            state->names.push_back(op);
        }
        managedWorkers_.push_back(state);
    }

    bool revokeManagedOperation(const std::string& pluginId,
                                const std::string& operation)
    {
        bool revoked=false;
        for(size_t i=0;i<managedWorkers_.size();++i)
            if(managedWorkers_[i]->pluginId==pluginId)
                revoked=managedWorkers_[i]->permissions.revokeMethod(operation)||revoked;
        return revoked;
    }

    bool revokeManagedWorker(const std::string& pluginId)
    {
        bool revoked=false;
        for(size_t i=0;i<managedWorkers_.size();)
        {
            const std::shared_ptr<ManagedWorkerState> state=managedWorkers_[i];
            if(state->pluginId!=pluginId){++i;continue;}
            revoked=true;
            state->accepting.store(false);
            state->permissions.revokeAllMethods();
            state->registrations.clear();
            const std::shared_ptr<detail::ProcessBroker> broker=state->broker;
            managedWorkers_.erase(managedWorkers_.begin()+i);
            std::thread([broker](){broker->stop();}).detach();
        }
        return revoked;
    }

    void bind(
        const std::string& method,
        const DynamicFunction& function)
    {
        requireNotManaged(method);
        if (method.find("__native_object.") == 0)
        {
            throw Error(
                "reserved_method",
                "NativeWeb internal object method name is reserved");
        }

        runtime_.bind(method, function);
    }

    void bindCallable(
        const std::string& method,
        const detail::Callable& callable)
    {
        requireNotManaged(method);
        if (method.find("__native_object.") == 0)
        {
            throw Error(
                "reserved_method",
                "NativeWeb internal object method name is reserved");
        }

        runtime_.bind(method, callable);
    }

    NativeObjectHandle addObject(
        const std::shared_ptr<void>& object,
        const std::string& typeName)
    {
        return objects_->objects.add(object, typeName);
    }

    void bindObjectMethod(
        const NativeObjectHandle& handle,
        const std::string& method,
        const DynamicFunction& function)
    {
        objects_->objects.bindMethod(
            handle,
            method,
            function);
    }

    bool releaseObject(
        const NativeObjectHandle& handle)
    {
        return objects_->objects.release(handle);
    }

    std::future<Any> execute(
        const std::string& method,
        const VariantList& args)
    {
        const std::shared_ptr<detail::PendingResult<Any> > pending(
            new detail::PendingResult<Any>());

        std::future<Any> result =
            pending->future();

        beginExecute(
            method,
            args,
            pending);

        return result;
    }

    void beginExecute(
        const std::string& method,
        const VariantList& args,
        const std::shared_ptr<detail::PendingResultBase>& result)
    {
        detail::BrowserBackend* backend =
            requireBackend();

        const detail::OutboundRequest call =
            runtime_.callWithPending(
                method,
                args,
                result);

        backend->postBridgeMessage(
            call.message);
    }

    void emit(
        const std::string& event,
        const Any& payload)
    {
        if (event.empty() || event.size() > 256)
            throw std::invalid_argument("Message channel must be 1..256 bytes");
        requireBackend()->postBridgeMessage(
            runtime_.eventMessage(event, payload));
    }

    MessageSubscriptionId onMessage(
        const std::string& channel,
        const MessageCallback& callback)
    {
        if (channel.empty() || channel.size() > 256 || !callback)
            throw std::invalid_argument("Invalid message channel or callback");
        return runtime_.subscribe(channel, callback);
    }

    bool offMessage(MessageSubscriptionId subscription)
    {
        return runtime_.unsubscribe(subscription);
    }

    void onBrowserCreated() override
    {
        if (listener_)
            listener_->onCreated();
    }

    void onLoadStarted(const std::string& source) override
    {
        revokeManagedWorkers();
        invalidateDocument();
        if (listener_)
            listener_->onLoadStarted(source);
    }

    void onLoadFinished(const std::string& source) override
    {
        if (listener_)
            listener_->onLoadFinished(source);
    }

    void onTrustedFrameActivated(const detail::VerifiedFrameContext& ctx) override
    {
        if (ctx.engineVerified && ctx.mainFrame && ctx.trustedContent &&
            ctx.frameId && ctx.navigation)
            trustedFrame_ = ctx;
    }

    void onBridgeMessage(const Any& message) override
    {
        dispatchBridgeMessage(message,nullptr);
    }

    void onVerifiedBridgeMessage(const Any& message,
                                 const detail::VerifiedFrameContext& ctx) override
    {
        dispatchBridgeMessage(message,&ctx);
    }

    void dispatchBridgeMessage(const Any& message,
                               const detail::VerifiedFrameContext* ctx)
    {
        const detail::BridgeMessage parsed=detail::parseBridgeMessage(message);
        if(parsed.type==detail::BridgeMessageType::Event)
        {
            // One-way messages are privileged native callbacks too. Require
            // engine provenance and the exact active top-level navigation.
            if(!ctx || !ctx->engineVerified || !ctx->mainFrame ||
               !ctx->trustedContent || !ctx->frameId || !ctx->navigation ||
               !trustedFrame_.engineVerified ||
               !trustedFrame_.mainFrame || !trustedFrame_.trustedContent ||
               ctx->frameId!=trustedFrame_.frameId ||
               ctx->navigation!=trustedFrame_.navigation)
                return;
        }
        if(parsed.type==detail::BridgeMessageType::Request)
        {
            for(std::size_t i=0;i<managedWorkers_.size();++i)
            {
                const std::shared_ptr<ManagedWorkerState> state=managedWorkers_[i];
                for(std::size_t j=0;j<state->names.size();++j)
                {
                    if(state->names[j]!=parsed.method)continue;
                    if(!ctx || !state->permissions.allows(*ctx,parsed.method))
                    {
                        const Any denied=detail::makeErrorMessage(
                            parsed.requestId,Error("permission_denied",
                                "Managed operation denied for caller frame"));
                        const std::shared_ptr<ResponseSinkState> sink=responseSink_;
                        std::lock_guard<std::mutex> lock(sink->mutex);
                        if(sink->owner && sink->owner->backend_)
                            sink->owner->backend_->postBridgeMessage(denied);
                        return;
                    }
                    break;
                }
            }
        }
        const std::shared_ptr<ResponseSinkState> sink = responseSink_;
        std::uint64_t docGeneration=0;
        {
            std::lock_guard<std::mutex> lock(sink->mutex);
            docGeneration=sink->generation;
        }
        runtime_.receiveAsync(
            message,
            [sink,docGeneration](const Any& response) {
                if (!sink || response.empty())
                    return;

                std::lock_guard<std::mutex> lock(
                    sink->mutex);

                Impl* owner = sink->owner;

                if (owner && owner->backend_ && sink->generation==docGeneration)
                    owner->backend_->postBridgeMessage(
                        response);
            });
    }

    void onBrowserClosed() override
    {
        revokeManagedWorkers();
        invalidateDocument();
        detachResponseSink();
        runtime_.shutdown();
        objects_->stop();

        if (listener_)
            listener_->onClosed();
    }

private:
    void requireNotManaged(const std::string& method) const
    {
        for(size_t i=0;i<managedWorkers_.size();++i)
            for(size_t j=0;j<managedWorkers_[i]->names.size();++j)
                if(managedWorkers_[i]->names[j]==method)
                    throw Error("reserved_method","Managed method cannot be overwritten");
    }
    void invalidateDocument()
    {
        trustedFrame_ = detail::VerifiedFrameContext();
        // Do not let a native->JS std::future wait for a Promise belonging to
        // a document that has navigated away. Preserve ordinary bindings and
        // permit fresh calls in the new document. Request IDs stay monotonic.
        {
            std::lock_guard<std::mutex> lock(responseSink_->mutex);
            ++responseSink_->generation;
        }
        runtime_.cancelAllIncoming();
        runtime_.rejectPending(Error("navigation_changed",
            "JavaScript document changed before call completion"));
    }
    void revokeManagedWorkers()
    {
        std::vector<std::shared_ptr<ManagedWorkerState> > old;
        old.swap(managedWorkers_);
        for(size_t i=0;i<old.size();++i){
            old[i]->accepting.store(false);
            old[i]->permissions.revokeAllMethods();
            old[i]->registrations.clear();
            const std::shared_ptr<detail::ProcessBroker> broker=old[i]->broker;
            std::thread([broker](){broker->stop();}).detach();
        }
    }
    void detachResponseSink()
    {
        if (!responseSink_)
            return;

        std::lock_guard<std::mutex> lock(
            responseSink_->mutex);

        responseSink_->owner = 0;
    }

    static NativeObjectHandle parseObjectHandle(
        const std::string& idText,
        const std::string& typeName)
    {
        std::istringstream stream(idText);
        std::uint64_t id = 0;
        stream >> id;

        if (!stream || !stream.eof() || id == 0)
        {
            throw Error(
                "invalid_native_object",
                "Native object id is invalid");
        }

        return NativeObjectHandle(id, typeName);
    }

    detail::BrowserBackend* requireBackend()
    {
        if (!backend_ || !backend_->isCreated())
        {
            throw Error(
                "webview_not_created",
                "NativeWeb WebView has not been created");
        }

        return backend_.get();
    }

    std::shared_ptr<ResponseSinkState> responseSink_;
    detail::VerifiedFrameContext trustedFrame_;
    detail::BridgeRuntime runtime_;
    std::shared_ptr<ObjectBridgeState> objects_;
    std::map<std::string, LoadedPlugin> plugins_;
    std::vector<std::shared_ptr<ManagedWorkerState> > managedWorkers_;
    std::string packagedEntry_;
    std::mutex pluginsMutex_;
    std::unique_ptr<detail::BrowserBackend> backend_;
    WebViewListener* listener_;
    Engine resolvedEngine_;
};

WebView::WebView()
    : impl_(new Impl())
{
}

WebView::~WebView()
{
}

WebView::WebView(WebView&& other)
    : impl_(std::move(other.impl_))
{
    if (!impl_)
        impl_.reset(new Impl());
}

WebView& WebView::operator=(WebView&& other)
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);

        if (!impl_)
            impl_.reset(new Impl());
    }

    return *this;
}

void WebView::create(
    NativeWindowHandle parent,
    const std::string& source)
{
    create(parent, source, WebViewOptions());
}

void WebView::create(
    NativeWindowHandle parent,
    const std::string& source,
    const WebViewOptions& options)
{
    impl_->create(parent, source, options);
}

void WebView::destroy()
{
    impl_->destroy();
}

void WebView::setListener(WebViewListener* listener)
{
    impl_->setListener(listener);
}

bool WebView::isCreated() const
{
    return impl_->isCreated();
}

Engine WebView::engine() const
{
    return impl_->engine();
}

Capabilities WebView::capabilities() const
{
    return impl_->capabilities();
}

void WebView::load(const std::string& source)
{
    impl_->load(source);
}

void WebView::reload()
{
    impl_->reload();
}

std::string WebView::loadPlugin(const std::string& path)
{
    return impl_->loadPlugin(path);
}

void WebView::bindPluginFactory(const std::string& method,
                               const std::string& pluginId,
                               const std::string& relativeType)
{
    impl_->bindPluginFactory(method, pluginId, relativeType);
}

NativeObjectHandle WebView::createPluginObject(const std::string& pluginId,
                                               const std::string& relativeType,
                                               const VariantList& args)
{
    return impl_->createPluginObject(pluginId, relativeType, args);
}

void WebView::unloadPlugin(const std::string& pluginId)
{
    impl_->unloadPlugin(pluginId);
}

void WebView::bindManagedWorker(const std::string& plugin,
    const std::vector<std::string>& operations,
    const std::vector<std::string>& command,unsigned timeoutMs)
{
    impl_->bindManagedWorker(plugin,operations,command,timeoutMs);
}

bool WebView::revokeManagedOperation(const std::string& pluginId,
                                     const std::string& operation)
{
    return impl_->revokeManagedOperation(pluginId,operation);
}

bool WebView::revokeManagedWorker(const std::string& pluginId)
{
    return impl_->revokeManagedWorker(pluginId);
}

void WebView::bind(
    const std::string& method,
    const DynamicFunction& function)
{
    impl_->bind(method, function);
}

void WebView::bindCallable(
    const std::string& method,
    const detail::Callable& callable)
{
    impl_->bindCallable(
        method,
        callable);
}

std::future<Any> WebView::execute(
    const std::string& method,
    const VariantList& args)
{
    return impl_->execute(method, args);
}

void WebView::postMessage(
    const std::string& channel,
    const Any& payload)
{
    impl_->emit(channel, payload);
}

MessageSubscriptionId WebView::onMessage(
    const std::string& channel,
    const MessageCallback& callback)
{
    return impl_->onMessage(channel, callback);
}

bool WebView::offMessage(MessageSubscriptionId subscription)
{
    return impl_->offMessage(subscription);
}

void WebView::emit(
    const std::string& event,
    const Any& payload)
{
    postMessage(event, payload);
}

void WebView::beginExecute(
    const std::string& method,
    const VariantList& args,
    const std::shared_ptr<detail::PendingResultBase>& result)
{
    impl_->beginExecute(
        method,
        args,
        result);
}

NativeObjectHandle WebView::addObject(
    const std::shared_ptr<void>& object,
    const std::string& typeName)
{
    return impl_->addObject(object, typeName);
}

void WebView::bindObjectMethod(
    const NativeObjectHandle& handle,
    const std::string& method,
    const DynamicFunction& function)
{
    impl_->bindObjectMethod(
        handle,
        method,
        function);
}

bool WebView::releaseObject(
    const NativeObjectHandle& handle)
{
    return impl_->releaseObject(handle);
}

} // namespace nativeweb
