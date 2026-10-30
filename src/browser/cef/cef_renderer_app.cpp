#include "browser/cef/cef_renderer_app.hpp"

#include "browser/cef/cef_value_converter.hpp"
#include "include/cef_process_message.h"
#include "include/cef_scheme.h"

#include <iostream>
#include <sstream>
#include <vector>

namespace nativeweb {
namespace detail {

namespace {

#ifdef NATIVEWEB_CEF_ENABLE_E2E_REPLAY_HOOKS
// Test-build only. An old token stays in the CEF renderer, never in page JS.
std::map<int, std::string> retiredSessionForE2E;
#endif

bool resolveDottedFunction(
    CefRefPtr<CefV8Context> context,
    const std::string& method,
    CefRefPtr<CefV8Value>& receiver,
    CefRefPtr<CefV8Value>& function)
{
    if (!context || method.empty())
        return false;

    CefRefPtr<CefV8Value> current = context->GetGlobal();
    std::size_t start = 0;

    while (start < method.size())
    {
        const std::size_t dot = method.find('.', start);
        const std::string part =
            method.substr(
                start,
                dot == std::string::npos
                    ? std::string::npos
                    : dot - start);

        if (part.empty() || !current || !current->IsObject())
            return false;

        CefRefPtr<CefV8Value> next =
            current->GetValue(part);

        if (dot == std::string::npos)
        {
            if (!next || !next->IsFunction())
                return false;

            receiver = current;
            function = next;
            return true;
        }

        current = next;
        start = dot + 1;
    }

    return false;
}

void sendCallError(
    CefRefPtr<CefFrame> frame,
    const std::string& id,
    const std::string& code,
    const std::string& message,
    const std::string& documentSession = std::string())
{
    CefRefPtr<CefProcessMessage> response =
        CefProcessMessage::Create("nativeweb.call.response");

    CefRefPtr<CefListValue> args =
        response->GetArgumentList();

    args->SetString(0, id);
    args->SetBool(1, false);
    args->SetString(2, code);
    args->SetString(3, message);
    args->SetString(4, documentSession);

    frame->SendProcessMessage(PID_BROWSER, response);
}

void sendCallSuccess(
    CefRefPtr<CefFrame> frame,
    const std::string& id,
    CefRefPtr<CefValue> value,
    const std::string& documentSession)
{
    CefRefPtr<CefProcessMessage> response =
        CefProcessMessage::Create("nativeweb.call.response");

    CefRefPtr<CefListValue> args =
        response->GetArgumentList();

    args->SetString(0, id);
    args->SetBool(1, true);
    args->SetValue(2, value);
    args->SetString(4, documentSession);

    frame->SendProcessMessage(PID_BROWSER, response);
}

std::string promiseRejectionMessage(
    CefRefPtr<CefV8Value> value)
{
    if (!value)
        return "JavaScript Promise rejected";

    if (value->IsString())
        return value->GetStringValue().ToString();

    if (value->IsObject() && value->HasValue("message"))
    {
        CefRefPtr<CefV8Value> message =
            value->GetValue("message");

        if (message && message->IsString())
            return message->GetStringValue().ToString();
    }

    return "JavaScript Promise rejected";
}

class CallPromiseHandler : public CefV8Handler
{
public:
    CallPromiseHandler(
        CefRefPtr<CefFrame> frame,
        const std::string& id,
        bool success,
        const std::string& documentSession)
        : frame_(frame),
          id_(id),
          success_(success),
          documentSession_(documentSession)
    {
    }

    bool Execute(
        const CefString& name,
        CefRefPtr<CefV8Value> object,
        const CefV8ValueList& arguments,
        CefRefPtr<CefV8Value>& retval,
        CefString& exception) override
    {
        if (success_)
        {
            CefRefPtr<CefV8Value> value =
                arguments.empty()
                    ? CefV8Value::CreateNull()
                    : arguments[0];

            sendCallSuccess(
                frame_,
                id_,
                v8ToCefValue(value),
                documentSession_);
        }
        else
        {
            sendCallError(
                frame_,
                id_,
                "js_promise_rejected",
                promiseRejectionMessage(
                    arguments.empty()
                        ? nullptr
                        : arguments[0]),
                documentSession_);
        }

        retval = CefV8Value::CreateUndefined();
        return true;
    }

private:
    CefRefPtr<CefFrame> frame_;
    std::string id_;
    bool success_;
    std::string documentSession_;

    IMPLEMENT_REFCOUNTING(CallPromiseHandler);
};

} // namespace

class CefRendererApp::InvokeHandler : public CefV8Handler
{
public:
    explicit InvokeHandler(CefRendererApp* app)
        : app_(app)
    {
    }

    bool Execute(
        const CefString& name,
        CefRefPtr<CefV8Value> object,
        const CefV8ValueList& arguments,
        CefRefPtr<CefV8Value>& retval,
        CefString& exception) override
    {
        CefRefPtr<CefV8Context> context =
            CefV8Context::GetCurrentContext();

        if (!context)
        {
            exception = "NativeWeb V8 context is unavailable";
            return true;
        }

#ifdef NATIVEWEB_CEF_ENABLE_E2E_REPLAY_HOOKS
        if (name == "testCaptureRetiredSession" ||
            name == "testReplayRetiredSession")
        {
            retval = CefV8Value::CreateBool(false);
            CefRefPtr<CefFrame> frame = context->GetFrame();
            CefRefPtr<CefBrowser> browser = context->GetBrowser();
            if (!frame || !frame->IsMain() || !browser) return true;

            std::string currentToken;
            for (std::size_t i = 0; i < app_->documentSessions_.size(); ++i)
            {
                const CefRendererApp::DocumentSession& session =
                    app_->documentSessions_[i];
                if (session.context && session.context->IsSame(context))
                {
                    currentToken = session.token;
                    break;
                }
            }
            if (currentToken.size() != 48) return true;

            const int browserId = browser->GetIdentifier();
            if (name == "testCaptureRetiredSession")
            {
                retiredSessionForE2E[browserId] = currentToken;
                retval = CefV8Value::CreateBool(true);
                return true;
            }
            std::map<int, std::string>::const_iterator old =
                retiredSessionForE2E.find(browserId);
            if (old == retiredSessionForE2E.end() ||
                old->second.size() != 48 || old->second == currentToken ||
                arguments.size() != 3 ||
                !arguments[0]->IsString() ||
                !arguments[1]->IsString() ||
                !arguments[2]->IsString())
                return true;
            const std::string kind = arguments[0]->GetStringValue().ToString();
            const std::string requestId = arguments[1]->GetStringValue().ToString();
            const std::string method = arguments[2]->GetStringValue().ToString();
            if (requestId.empty() ||
                (kind != "request" && kind != "cancel" && kind != "event") ||
                (kind != "cancel" && method.empty()))
                return true;

            CefRefPtr<CefProcessMessage> replay =
                CefProcessMessage::Create(
                    kind == "cancel" ? "nativeweb.cancel" :
                    kind == "event" ? "nativeweb.client.event" : "nativeweb.request");
            CefRefPtr<CefListValue> wire = replay->GetArgumentList();
            wire->SetString(0, requestId);
            if (kind == "cancel")
                wire->SetString(1, old->second);
            else if (kind == "event")
            {
                wire->SetString(0, method);
                wire->SetNull(1);
                wire->SetString(3, old->second);
            }
            else
            {
                wire->SetString(1, method);
                CefRefPtr<CefListValue> emptyArgs = CefListValue::Create();
                emptyArgs->SetSize(0);
                wire->SetList(2, emptyArgs);
                wire->SetString(3, old->second);
            }
            frame->SendProcessMessage(PID_BROWSER, replay);
            retval = CefV8Value::CreateBool(true);
            return true;
        }
#endif

        if (name == "invoke" || name == "invokeRaw" || name == "invokeCancelableRaw")
        {
            if (arguments.empty() || !arguments[0]->IsString())
            {
                exception =
                    "native.invoke(method, ...args) requires a method string";
                return true;
            }

            CefRefPtr<CefFrame> frame = context->GetFrame();
            CefRefPtr<CefV8Value> promise =
                CefV8Value::CreatePromise();

            if (!promise)
            {
                exception = "Failed to create NativeWeb Promise";
                return true;
            }

            CefV8ValueList callArguments;
            for (std::size_t i = 1; i < arguments.size(); ++i)
                callArguments.push_back(arguments[i]);

            const std::string id=app_->beginInvoke(
                frame,context,promise,
                arguments[0]->GetStringValue(),callArguments);
            if(name=="invokeCancelableRaw")
            {
                CefRefPtr<CefV8Value> operation=
                    CefV8Value::CreateObject(nullptr,nullptr);
                operation->SetValue("id",CefV8Value::CreateString(id),
                                    V8_PROPERTY_ATTRIBUTE_READONLY);
                operation->SetValue("promise",promise,
                                    V8_PROPERTY_ATTRIBUTE_READONLY);
                retval=operation;
            }
            else retval=promise;
            return true;
        }

        if (name == "cancelRaw")
        {
            if ((arguments.size()!=1 && arguments.size()!=2) ||
                !arguments[0]->IsString() ||
                (arguments.size()==2 && !arguments[1]->IsString()))
            {
                exception="cancelRaw requires an ID and optional reason string";
                return true;
            }
            const std::string reason =
                arguments.size()>1 && arguments[1]->IsString() &&
                arguments[1]->GetStringValue().ToString()=="request_timeout"
                    ? "request_timeout" : "request_cancelled";
            retval=CefV8Value::CreateBool(
                app_->cancelInvoke(
                    arguments[0]->GetStringValue().ToString(),context,reason));
            return true;
        }

        if (name == "postMessageRaw")
        {
            if (arguments.empty() || arguments.size() > 2 ||
                !arguments[0]->IsString())
            {
                exception = "postMessage(channel, payload) requires a channel string";
                return true;
            }
            const std::string channel = arguments[0]->GetStringValue().ToString();
            if (channel.empty() || channel.size() > 256)
            {
                exception = "Message channel must be 1..256 bytes";
                return true;
            }
            CefRefPtr<CefFrame> frame = context->GetFrame();
            if (!frame || !frame->IsMain())
            {
                exception = "permission_denied: not main frame";
                return true;
            }
            CefRefPtr<CefProcessMessage> wire =
                CefProcessMessage::Create("nativeweb.client.event");
            CefRefPtr<CefListValue> slots = wire->GetArgumentList();
            slots->SetString(0, channel);
            if (arguments.size() == 2)
                slots->SetValue(1, v8ToCefValue(arguments[1]));
            else
                slots->SetNull(1);
            std::string session;
            for (std::size_t i=0; i<app_->documentSessions_.size(); ++i)
                if (app_->documentSessions_[i].context &&
                    app_->documentSessions_[i].context->IsSame(context))
                {
                    session=app_->documentSessions_[i].token;
                    break;
                }
            if (!session.empty())
            {
                slots->SetString(3, session);
                frame->SendProcessMessage(PID_BROWSER, wire);
            }
            else if (app_->queuedDocumentRequests_.size() < 256)
            {
                CefRendererApp::QueuedDocumentRequest queued;
                queued.frame=frame;
                queued.context=context;
                queued.message=wire;
                app_->queuedDocumentRequests_.push_back(queued);
            }
            else
                exception = "session_unavailable: pending queue full";
            return true;
        }

        if (name == "on" || name == "onRaw")
        {
            if (arguments.size() != 2 ||
                !arguments[0]->IsString() ||
                !arguments[1]->IsFunction())
            {
                exception =
                    "native.on(eventName, callback) requires a string and function";
                return true;
            }

            const std::string id =
                app_->subscribeEvent(
                    context,
                    arguments[0]->GetStringValue(),
                    arguments[1]);

            retval = CefV8Value::CreateString(id);
            return true;
        }

        if (name == "off" || name == "offRaw")
        {
            if (arguments.size() != 1 || !arguments[0]->IsString())
            {
                exception =
                    "native.off(subscriptionId) requires a subscription id string";
                return true;
            }

            retval = CefV8Value::CreateBool(
                app_->unsubscribeEvent(
                    arguments[0]->GetStringValue().ToString()));

            return true;
        }

        return false;
    }

private:
    CefRendererApp* app_;

    IMPLEMENT_REFCOUNTING(InvokeHandler);
};

CefRendererApp::CefRendererApp()
    : nextRequestId_(1),
      nextSubscriptionId_(1)
{
}

void CefRendererApp::setBrowserReadyCallback(
    const std::function<void()>& callback)
{
    browserReadyCallback_ = callback;
}

void CefRendererApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar)
{
    if (registrar)
        registrar->AddCustomScheme(
            "app",
            CEF_SCHEME_OPTION_STANDARD |
            CEF_SCHEME_OPTION_SECURE |
            CEF_SCHEME_OPTION_CORS_ENABLED |
            CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void CefRendererApp::OnContextInitialized()
{
    if (browserReadyCallback_)
        browserReadyCallback_();
}

void CefRendererApp::OnContextCreated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context)
{
    CefRefPtr<CefV8Value> global = context->GetGlobal();
    CefRefPtr<CefV8Value> native =
        CefV8Value::CreateObject(nullptr, nullptr);

    CefRefPtr<InvokeHandler> handler =
        new InvokeHandler(this);

    CefRefPtr<CefV8Value> invokeRaw =
        CefV8Value::CreateFunction(
            "invokeRaw",
            handler);

    CefRefPtr<CefV8Value> onRaw =
        CefV8Value::CreateFunction(
            "onRaw",
            handler);

    CefRefPtr<CefV8Value> offRaw =
        CefV8Value::CreateFunction(
            "offRaw",
            handler);

    native->SetValue(
        "invokeRaw",invokeRaw,V8_PROPERTY_ATTRIBUTE_READONLY);
    native->SetValue(
        "invokeCancelableRaw",
        CefV8Value::CreateFunction("invokeCancelableRaw",handler),
        V8_PROPERTY_ATTRIBUTE_READONLY);
    native->SetValue(
        "cancelRaw",
        CefV8Value::CreateFunction("cancelRaw",handler),
        V8_PROPERTY_ATTRIBUTE_READONLY);

    native->SetValue(
        "postMessageRaw",
        CefV8Value::CreateFunction("postMessageRaw",handler),
        V8_PROPERTY_ATTRIBUTE_READONLY);

    native->SetValue(
        "onRaw",
        onRaw,
        V8_PROPERTY_ATTRIBUTE_READONLY);

    native->SetValue(
        "offRaw",
        offRaw,
        V8_PROPERTY_ATTRIBUTE_READONLY);

#ifdef NATIVEWEB_CEF_ENABLE_E2E_REPLAY_HOOKS
    // Available only when the CEF E2E integration target is built.
    native->SetValue("testCaptureRetiredSession",
        CefV8Value::CreateFunction("testCaptureRetiredSession", handler),
        V8_PROPERTY_ATTRIBUTE_READONLY);
    native->SetValue("testReplayRetiredSession",
        CefV8Value::CreateFunction("testReplayRetiredSession", handler),
        V8_PROPERTY_ATTRIBUTE_READONLY);
#endif

    global->SetValue(
        "native",
        native,
        V8_PROPERTY_ATTRIBUTE_READONLY);

    static const char kWrapperScript[] =
        "(function(native,global){"
        "const wrap=(value)=>{"
        "if(!value||typeof value!=='object')return value;"
        "if(value instanceof ArrayBuffer)return value;"
        "if(value.__nativeweb_object===true){"
        "const target={"
        "__nativeweb_object:true,"
        "id:String(value.id),"
        "type:String(value.type||'')"
        "};"
        "return new Proxy(target,{get(t,p){"
        "if(p==='then')return undefined;"
        "if(p==='__nativewebObjectId')return t.id;"
        "if(p==='__nativewebType')return t.type;"
        "if(p==='dispose')return ()=>"
        "native.invokeRaw('__native_object.release',t.id,t.type);"
        "if(p in t)return t[p];"
        "if(typeof p!=='string')return undefined;"
        "return (...args)=>native.invokeRaw("
        "'__native_object.call',t.id,t.type,p,...args"
        ").then(wrap);"
        "}});"
        "}"
        "if(Array.isArray(value))return value.map(wrap);"
        "for(const key of Object.keys(value)){"
        "value[key]=wrap(value[key]);"
        "}"
        "return value;"
        "};"
        // CEF's public CefV8Value interface handles ArrayBuffer, but not
        // typed-array view offsets. Normalize nested views before V8 IPC.
        "const prepare=(value)=>{"
        "if(value instanceof ArrayBuffer)return value;"
        "if(ArrayBuffer.isView(value))"
        "return value.buffer.slice(value.byteOffset,value.byteOffset+value.byteLength);"
        "if(Array.isArray(value))return value.map(prepare);"
        "if(value&&typeof value==='object'&&"
        "value.__nativeweb_object!==true&&!value.__nativewebObjectId){"
        "const out={};for(const key of Object.keys(value))out[key]=prepare(value[key]);"
        "return out;}"
        "return value;"
        "};"
        "const invoke=(method,...args)=>"
        "native.invokeRaw(method,...args.map(prepare)).then(wrap);"
        "native.invoke=invoke;"
        "const invokeWithOptions=(method,options,...args)=>{"
        "const opts=options==null?{}:options;"
        "if(typeof opts!=='object')return Promise.reject(new TypeError('options must be an object'));"
        "const timeoutMs=opts.timeoutMs;"
        "if(timeoutMs!==undefined&&(!Number.isInteger(timeoutMs)||timeoutMs<1||timeoutMs>60000))"
        "return Promise.reject(new RangeError('timeoutMs must be 1..60000'));"
        "const signal=opts.signal;"
        "if(signal!=null&&(typeof signal.addEventListener!=='function'||"
        "typeof signal.removeEventListener!=='function'||typeof signal.aborted!=='boolean'))"
        "return Promise.reject(new TypeError('signal must be AbortSignal'));"
        "if(signal&&signal.aborted)return Promise.reject(new Error('request_cancelled: already aborted'));"
        "const operation=native.invokeCancelableRaw(method,...args.map(prepare));"
        "const abort=()=>native.cancelRaw(operation.id,'request_cancelled');"
        "if(signal)signal.addEventListener('abort',abort,{once:true});"
        "const timer=timeoutMs===undefined?null:setTimeout("
        "()=>native.cancelRaw(operation.id,'request_timeout'),timeoutMs);"
        "const cleanup=()=>{if(timer!==null)clearTimeout(timer);"
        "if(signal)signal.removeEventListener('abort',abort);};"
        "const result=operation.promise.then(wrap);"
        "result.then(cleanup,cleanup);"
        "if(signal&&signal.aborted)abort();"
        "return result;"
        "};"
        "native.invokeWithOptions=invokeWithOptions;"
        "native.on=(name,callback)=>"
        "native.onRaw(name,(payload)=>callback(wrap(payload)));"
        "native.off=(id)=>native.offRaw(id);"
        "native.postMessage=(channel,payload=null)=>"
        "native.postMessageRaw(channel,prepare(payload));"
        "native.onMessage=native.on;"
        "native.offMessage=native.off;"
        "const namespace=(path)=>new Proxy(function(){},{"
        "get(_target,property){"
        "if(property==='then')return undefined;"
        "if(path===''&&property==='invoke')return invoke;"
        "if(path===''&&property==='invokeWithOptions')return invokeWithOptions;"
        "if(path===''&&property==='on')return native.on;"
        "if(path===''&&property==='postMessage')return native.postMessage;"
        "if(path===''&&property==='onMessage')return native.onMessage;"
        "if(path===''&&property==='offMessage')return native.offMessage;"
        "if(path===''&&property==='off')return native.off;"
        "if(typeof property!=='string')return undefined;"
        "const next=path?path+'.'+property:property;"
        "return namespace(next);"
        "},"
        "apply(_target,_this,args){"
        "if(!path)throw new TypeError('xytron root is not callable');"
        "return invoke(path,...args);"
        "}"
        "});"
        "Object.defineProperty(global,'xytron',{"
        "value:namespace(''),"
        "writable:false,"
        "configurable:false,"
        "enumerable:true"
        "});"
        "})(native,globalThis);";

    CefRefPtr<CefV8Value> wrapperResult;
    CefRefPtr<CefV8Exception> wrapperException;

    context->Eval(
        kWrapperScript,
        frame ? frame->GetURL() : CefString(),
        0,
        wrapperResult,
        wrapperException);
}

void CefRendererApp::OnContextReleased(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context)
{
    for (std::vector<DocumentSession>::iterator it=documentSessions_.begin();
         it!=documentSessions_.end();)
    {
        if(it->context && it->context->IsSame(context))
            it=documentSessions_.erase(it);
        else ++it;
    }
    for (std::vector<QueuedDocumentRequest>::iterator it=
             queuedDocumentRequests_.begin(); it!=queuedDocumentRequests_.end();)
    {
        if(it->context && it->context->IsSame(context))
            it=queuedDocumentRequests_.erase(it);
        else ++it;
    }
    for (std::map<std::string, PendingPromise>::iterator it =
             pending_.begin();
         it != pending_.end();)
    {
        if (it->second.context.get() == context.get())
            pending_.erase(it++);
        else
            ++it;
    }

    for (std::map<std::string, EventSubscription>::iterator it =
             subscriptions_.begin();
         it != subscriptions_.end();)
    {
        if (it->second.context.get() == context.get())
            subscriptions_.erase(it++);
        else
            ++it;
    }
}

std::string CefRendererApp::beginInvoke(
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context,
    CefRefPtr<CefV8Value> promise,
    const CefString& method,
    const CefV8ValueList& arguments)
{
    std::ostringstream stream;
    stream << nextRequestId_++;
    const std::string id = stream.str();

    PendingPromise pending;
    pending.context = context;
    pending.promise = promise;
    pending_[id] = pending;

    if(!frame || !frame->IsMain())
    {
        promise->RejectPromise("permission_denied: not main frame");
        pending_.erase(id);
        return id;
    }

    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create("nativeweb.request");

    CefRefPtr<CefListValue> list =
        message->GetArgumentList();

    list->SetString(0, id);
    list->SetString(1, method);

    CefRefPtr<CefListValue> args =
        CefListValue::Create();
    args->SetSize(arguments.size());

    for (std::size_t i = 0; i < arguments.size(); ++i)
        args->SetValue(i, v8ToCefValue(arguments[i]));

    list->SetList(2, args);

    std::string session;
    for(std::size_t i=0;i<documentSessions_.size();++i)
        if(documentSessions_[i].context &&
           documentSessions_[i].context->IsSame(context))
        {
            session=documentSessions_[i].token;
            break;
        }
    if(!session.empty())
    {
        list->SetString(3,session);
        frame->SendProcessMessage(PID_BROWSER,message);
    }
    else if(queuedDocumentRequests_.size() < 256)
    {
        QueuedDocumentRequest queued;
        queued.frame=frame;
        queued.context=context;
        queued.message=message;
        queuedDocumentRequests_.push_back(queued);
    }
    else
    {
        pending_.erase(id);
        promise->RejectPromise("session_unavailable: pending queue full");
    }
    return id;
}

bool CefRendererApp::cancelInvoke(const std::string& id,
                                   CefRefPtr<CefV8Context> caller,
                                   const std::string& reason)
{
    std::map<std::string,PendingPromise>::iterator found=pending_.find(id);
    if(found==pending_.end() || !caller || !found->second.context ||
       !found->second.context->IsSame(caller))
        return false;
    const PendingPromise pending=found->second;
    pending_.erase(found);
    bool queued=false;
    for(std::vector<QueuedDocumentRequest>::iterator it=
            queuedDocumentRequests_.begin();it!=queuedDocumentRequests_.end();)
    {
        if(it->message->GetArgumentList()->GetString(0).ToString()==id)
        {
            it=queuedDocumentRequests_.erase(it);
            queued=true;
        }
        else ++it;
    }
    if(!queued && pending.context && pending.context->IsValid())
    {
        std::string token;
        for(std::size_t i=0;i<documentSessions_.size();++i)
            if(documentSessions_[i].context &&
               documentSessions_[i].context->IsSame(pending.context))
            {
                token=documentSessions_[i].token;
                break;
            }
        CefRefPtr<CefFrame> frame=pending.context->GetFrame();
        if(frame && !token.empty())
        {
            CefRefPtr<CefProcessMessage> cancel=
                CefProcessMessage::Create("nativeweb.cancel");
            cancel->GetArgumentList()->SetString(0,id);
            cancel->GetArgumentList()->SetString(1,token);
            frame->SendProcessMessage(PID_BROWSER,cancel);
        }
    }
    if(pending.promise)
        pending.promise->RejectPromise(
            reason + ": JavaScript request cancelled");
    return true;
}

std::string CefRendererApp::subscribeEvent(
    CefRefPtr<CefV8Context> context,
    const CefString& eventName,
    CefRefPtr<CefV8Value> callback)
{
    std::ostringstream stream;
    stream << nextSubscriptionId_++;
    const std::string id = stream.str();

    EventSubscription subscription;
    subscription.context = context;
    subscription.callback = callback;
    subscription.eventName = eventName.ToString();

    CefRefPtr<CefBrowser> browser =
        context ? context->GetBrowser() : nullptr;

    subscription.browserId =
        browser ? browser->GetIdentifier() : -1;

    subscriptions_[id] = subscription;
    return id;
}

bool CefRendererApp::unsubscribeEvent(
    const std::string& subscriptionId)
{
    return subscriptions_.erase(subscriptionId) != 0;
}

bool CefRendererApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId sourceProcess,
    CefRefPtr<CefProcessMessage> message)
{
    const CefString name = message->GetName();

    CefRefPtr<CefListValue> args =
        message->GetArgumentList();

    if (name == "nativeweb.document.session")
    {
        if(sourceProcess!=PID_BROWSER || !frame || !frame->IsMain())
            return true;
        CefRefPtr<CefV8Context> context=frame->GetV8Context();
        if (!context || !context->IsValid()) return true;
        const std::string token=args->GetString(0).ToString();
        if(token.size()!=48) return true;
        bool known=false;
        for(std::size_t i=0;i<documentSessions_.size();++i)
            if(documentSessions_[i].context &&
               documentSessions_[i].context->IsSame(context))
            {
                documentSessions_[i].token=token;
                known=true;
                break;
            }
        if(!known)
        {
            DocumentSession session;
            session.context=context;
            session.token=token;
            documentSessions_.push_back(session);
        }
        std::cerr<<"cef-document-session-received"<<std::endl;
        for(std::vector<QueuedDocumentRequest>::iterator it=
                queuedDocumentRequests_.begin();
            it!=queuedDocumentRequests_.end();)
        {
            if(it->context && it->context->IsSame(context))
            {
                std::cerr<<"cef-document-session-flush"<<std::endl;
                it->message->GetArgumentList()->SetString(3,token);
                it->frame->SendProcessMessage(PID_BROWSER,it->message);
                it=queuedDocumentRequests_.erase(it);
            }
            else ++it;
        }
        return true;
    }

    if (name == "nativeweb.call")
    {
        const std::string id =
            args->GetString(0).ToString();

        const std::string method =
            args->GetString(1).ToString();

        CefRefPtr<CefListValue> cefArguments =
            args->GetList(2);

        CefRefPtr<CefV8Context> context =
            frame ? frame->GetV8Context() : nullptr;

        if (!context || !context->Enter())
        {
            sendCallError(
                frame,
                id,
                "js_context_unavailable",
                "JavaScript context is unavailable");
            return true;
        }

        std::string currentSession;
        for(std::size_t i=0;i<documentSessions_.size();++i)
            if(documentSessions_[i].context &&
               documentSessions_[i].context->IsSame(context))
            {
                currentSession=documentSessions_[i].token;
                break;
            }
        CefRefPtr<CefV8Value> receiver;
        CefRefPtr<CefV8Value> function;

        if (!resolveDottedFunction(
                context,
                method,
                receiver,
                function))
        {
            context->Exit();
            sendCallError(
                frame,
                id,
                "js_method_not_found",
                "JavaScript method not found: " + method,
                currentSession);
            return true;
        }

        CefV8ValueList callArguments;
        const std::size_t count =
            cefArguments ? cefArguments->GetSize() : 0;
        callArguments.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            callArguments.push_back(
                cefValueToV8(
                    cefArguments->GetValue(i)));
        }

        CefRefPtr<CefV8Value> result =
            function->ExecuteFunction(
                receiver,
                callArguments);

        if (!result)
        {
            context->Exit();
            sendCallError(
                frame,
                id,
                "js_exception",
                "JavaScript method execution failed: " + method,
                currentSession);
            return true;
        }

        if (result->IsPromise())
        {
            CefRefPtr<CefV8Value> thenFunction =
                result->GetValue("then");

            if (!thenFunction ||
                !thenFunction->IsFunction())
            {
                context->Exit();
                sendCallError(
                    frame,
                    id,
                    "js_promise_invalid",
                    "JavaScript Promise has no callable then()",
                    currentSession);
                return true;
            }

            CefV8ValueList thenArguments;
            thenArguments.push_back(
                CefV8Value::CreateFunction(
                    "__nativewebResolve",
                    new CallPromiseHandler(
                        frame,
                        id,
                        true,
                        currentSession)));

            thenArguments.push_back(
                CefV8Value::CreateFunction(
                    "__nativewebReject",
                    new CallPromiseHandler(
                        frame,
                        id,
                        false,
                        currentSession)));

            CefRefPtr<CefV8Value> chained =
                thenFunction->ExecuteFunction(
                    result,
                    thenArguments);

            if (!chained)
            {
                context->Exit();
                sendCallError(
                    frame,
                    id,
                    "js_promise_attach_failed",
                    "Failed to attach NativeWeb Promise handlers",
                    currentSession);
                return true;
            }

            context->Exit();
            return true;
        }

        CefRefPtr<CefValue> cefResult =
            v8ToCefValue(result);

        context->Exit();

        sendCallSuccess(
            frame,
            id,
            cefResult,
            currentSession);

        return true;
    }

    if (name == "nativeweb.event")
    {
        const std::string eventName =
            args->GetString(0).ToString();

        CefRefPtr<CefValue> eventValue =
            args->GetValue(1);

        std::vector<EventSubscription> callbacks;

        for (std::map<std::string, EventSubscription>::const_iterator it =
                 subscriptions_.begin();
             it != subscriptions_.end();
             ++it)
        {
            if (it->second.eventName == eventName &&
                browser &&
                it->second.browserId ==
                    browser->GetIdentifier())
            {
                callbacks.push_back(it->second);
            }
        }

        for (std::size_t i = 0; i < callbacks.size(); ++i)
        {
            EventSubscription& subscription = callbacks[i];

            if (!subscription.context ||
                !subscription.callback ||
                !subscription.context->Enter())
            {
                continue;
            }

            CefV8ValueList callbackArgs;
            callbackArgs.push_back(
                cefValueToV8(eventValue));

            subscription.callback->ExecuteFunction(
                nullptr,
                callbackArgs);

            subscription.context->Exit();
        }

        return true;
    }

    if (name != "nativeweb.response")
        return false;

    const std::string id =
        args->GetString(0).ToString();

    std::map<std::string, PendingPromise>::iterator found =
        pending_.find(id);

    if (found == pending_.end())
        return true;

    PendingPromise pending = found->second;
    pending_.erase(found);

    if (!pending.context || !pending.promise)
        return true;

    if (!pending.context->Enter())
        return true;

    const bool success = args->GetBool(1);

    if (success)
    {
        pending.promise->ResolvePromise(
            cefValueToV8(args->GetValue(2)));
    }
    else
    {
        const std::string code =
            args->GetString(2).ToString();
        const std::string errorMessage =
            args->GetString(3).ToString();

        pending.promise->RejectPromise(
            code + ": " + errorMessage);
    }

    pending.context->Exit();
    return true;
}

} // namespace detail
} // namespace nativeweb
