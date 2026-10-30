#include "browser/cef/cef_backend.hpp"
#include "nativeweb/app_resources.hpp"

#include "browser/cef/cef_value_converter.hpp"
#include "browser/common/backend_registry.hpp"
#include "browser/common/trusted_document.hpp"
#include "browser/common/document_session_token.hpp"
#include "core/bridge_message.hpp"
#include "include/cef_process_message.h"
#include "include/cef_render_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_task.h"
#include "include/cef_scheme.h"
#include "include/cef_resource_handler.h"
#include "include/cef_request_context.h"
#include "include/cef_request_context_handler.h"
#include <cstring>
#include <algorithm>

#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace nativeweb {
namespace detail {

namespace {

std::string idToString(RequestId id)
{
    std::ostringstream stream;
    stream << id;
    return stream.str();
}

RequestId stringToId(const std::string& value)
{
    std::istringstream stream(value);
    RequestId id = 0;
    stream >> id;

    if (!stream || !stream.eof())
        throw std::runtime_error("Invalid CEF NativeWeb request id");

    return id;
}

// Each app:// handler owns its response bytes; it never opens a path on disk.
// A dedicated CefRequestContext owns the factory per WebView, so unrelated
// apps/windows cannot overwrite one another's asset table.
class AppResourceHandler : public CefResourceHandler
{
public:
    AppResourceHandler(const AppResource& resource, bool found)
        : mime_(found ? resource.mimeType : "text/plain"),
          bytes_(found ? resource.bytes : "Not Found"),
          status_(found ? 200 : 404), offset_(0) {}

    bool Open(CefRefPtr<CefRequest> request, bool& handle_request,
              CefRefPtr<CefCallback> callback) override
    {
        handle_request = true;
        return true;
    }

    void GetResponseHeaders(CefRefPtr<CefResponse> response,
                            int64_t& response_length,
                            CefString& redirect_url) override
    {
        response->SetStatus(status_);
        response->SetMimeType(mime_);
        CefResponse::HeaderMap headers;
        headers.insert(std::make_pair(
            CefString("X-Content-Type-Options"), CefString("nosniff")));
        headers.insert(std::make_pair(
            CefString("Cache-Control"), CefString("no-store")));
        response->SetHeaderMap(headers);
        response_length=static_cast<int64_t>(bytes_.size());
    }

    bool Read(void* data_out, int bytes_to_read, int& bytes_read,
              CefRefPtr<CefResourceReadCallback> callback) override
    {
        bytes_read=0;
        if (bytes_to_read <= 0 || offset_ >= bytes_.size()) return false;
        const std::size_t count=std::min(
            static_cast<std::size_t>(bytes_to_read), bytes_.size()-offset_);
        std::memcpy(data_out, bytes_.data()+offset_, count);
        offset_+=count;
        bytes_read=static_cast<int>(count);
        return true;
    }

    void Cancel() override {}

private:
    std::string mime_;
    std::string bytes_;
    int status_;
    std::size_t offset_;
    IMPLEMENT_REFCOUNTING(AppResourceHandler);
};

class AppResourceFactory : public CefSchemeHandlerFactory
{
public:
    explicit AppResourceFactory(
        const std::shared_ptr<const AppResourceBundle>& bundle)
        : bundle_(bundle) {}

    CefRefPtr<CefResourceHandler> Create(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& scheme_name,
        CefRefPtr<CefRequest> request) override
    {
        if (!request || scheme_name.ToString() != "app" || !bundle_)
            return nullptr;
        const std::string url=request->GetURL().ToString();
        AppResource resource;
        const bool found=bundle_->find(url, &resource);
        return new AppResourceHandler(resource, found);
    }

private:
    std::shared_ptr<const AppResourceBundle> bundle_;
    IMPLEMENT_REFCOUNTING(AppResourceFactory);
};

class FunctionTask : public CefTask
{
public:
    explicit FunctionTask(
        const std::function<void()>& function)
        : function_(function)
    {
    }

    void Execute() override
    {
        if (function_)
            function_();
    }

private:
    std::function<void()> function_;

    IMPLEMENT_REFCOUNTING(FunctionTask);
};

void runOnCefUi(
    const std::function<void()>& function)
{
    if (CefCurrentlyOn(TID_UI))
    {
        function();
        return;
    }

    CefPostTask(
        TID_UI,
        new FunctionTask(function));
}

void sendBridgeMessageToBrowser(
    CefRefPtr<CefBrowser> browser,
    const Any& message)
{
    if (!browser)
        return;

    const BridgeMessage parsed =
        parseBridgeMessage(message);

    if (parsed.type == BridgeMessageType::Request)
    {
        CefRefPtr<CefProcessMessage> callMessage =
            CefProcessMessage::Create("nativeweb.call");

        CefRefPtr<CefListValue> callArgs =
            callMessage->GetArgumentList();

        callArgs->SetString(
            0,
            idToString(parsed.requestId));

        callArgs->SetString(1, parsed.method);

        CefRefPtr<CefListValue> cefArguments =
            CefListValue::Create();

        cefArguments->SetSize(parsed.args.size());

        for (std::size_t i = 0;
             i < parsed.args.size();
             ++i)
        {
            cefArguments->SetValue(
                i,
                anyToCefValue(parsed.args[i]));
        }

        callArgs->SetList(2, cefArguments);

        browser->GetMainFrame()->SendProcessMessage(
            PID_RENDERER,
            callMessage);

        return;
    }

    if (parsed.type == BridgeMessageType::Event)
    {
        CefRefPtr<CefProcessMessage> eventMessage =
            CefProcessMessage::Create("nativeweb.event");

        CefRefPtr<CefListValue> eventArgs =
            eventMessage->GetArgumentList();

        eventArgs->SetString(0, parsed.eventName);
        eventArgs->SetValue(1, anyToCefValue(parsed.value));

        browser->GetMainFrame()->SendProcessMessage(
            PID_RENDERER,
            eventMessage);

        return;
    }

    if (parsed.type != BridgeMessageType::Response &&
        parsed.type != BridgeMessageType::Error)
    {
        return;
    }

    CefRefPtr<CefProcessMessage> cefMessage =
        CefProcessMessage::Create("nativeweb.response");

    CefRefPtr<CefListValue> args =
        cefMessage->GetArgumentList();

    args->SetString(0, idToString(parsed.requestId));

    if (parsed.type == BridgeMessageType::Response)
    {
        args->SetBool(1, true);
        args->SetValue(2, anyToCefValue(parsed.value));
    }
    else
    {
        args->SetBool(1, false);
        args->SetString(2, parsed.errorCode);
        args->SetString(3, parsed.errorMessage);
    }

    browser->GetMainFrame()->SendProcessMessage(
        PID_RENDERER,
        cefMessage);
}

} // namespace

class CefBackend::Client :
    public CefClient,
    public CefLifeSpanHandler,
    public CefLoadHandler,
    public CefRenderHandler,
    public CefRequestHandler
{
public:
    explicit Client(CefBackend* owner)
        : owner_(owner)
    {
    }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
    {
        return this;
    }

    CefRefPtr<CefLoadHandler> GetLoadHandler() override
    {
        return this;
    }

    CefRefPtr<CefRenderHandler> GetRenderHandler() override
    {
        return this;
    }

    CefRefPtr<CefRequestHandler> GetRequestHandler() override
    {
        return this;
    }

    void OnRenderProcessTerminated(
        CefRefPtr<CefBrowser> browser,
        TerminationStatus status,
        int errorCode,
        const CefString& errorString) override
    {
        std::cerr
            << "cef-renderer-terminated: browser="
            << (browser ? browser->GetIdentifier() : -1)
            << " status="
            << static_cast<int>(status)
            << " error="
            << errorCode
            << " message="
            << errorString.ToString()
            << std::endl;
    }

    void OnAfterCreated(
        CefRefPtr<CefBrowser> browser) override
    {
        owner_->onAfterCreated(browser);
    }

    void OnBeforeClose(
        CefRefPtr<CefBrowser> browser) override
    {
        owner_->onBeforeClose(browser);
    }

    void OnLoadStart(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        TransitionType transitionType) override
    {
        owner_->onLoadStart(frame);
    }

    void OnLoadEnd(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        int httpStatusCode) override
    {
        owner_->onLoadEnd(frame);
    }

    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefProcessId sourceProcess,
        CefRefPtr<CefProcessMessage> message) override
    {
        if (sourceProcess != PID_RENDERER)
            return false;

        return owner_->onProcessMessage(frame, message);
    }

    void GetViewRect(
        CefRefPtr<CefBrowser> browser,
        CefRect& rect) override
    {
        rect = CefRect(0, 0, 800, 600);
    }

    void OnPaint(
        CefRefPtr<CefBrowser> browser,
        PaintElementType type,
        const RectList& dirtyRects,
        const void* buffer,
        int width,
        int height) override
    {
    }

private:
    CefBackend* owner_;

    IMPLEMENT_REFCOUNTING(Client);
};

CefBackend::CefBackend()
    : listener_(0),
      client_(new Client(this))
{
}

CefBackend::~CefBackend()
{
}

const char* CefBackend::engineName() const
{
    return "cef";
}

Engine CefBackend::engine() const
{
    return Engine::Cef;
}

Capabilities CefBackend::capabilities() const
{
    return Capabilities(
        static_cast<std::uint64_t>(Capability::Binary) |
        static_cast<std::uint64_t>(Capability::Events) |
        static_cast<std::uint64_t>(Capability::AsyncJavaScript) |
        static_cast<std::uint64_t>(Capability::Windowless));
}

void CefBackend::setListener(
    BrowserBackendListener* listener)
{
    listener_ = listener;
}

void CefBackend::create(
    const BrowserCreateParams& params)
{
    if (browser_)
        throw std::runtime_error("CEF browser is already created");

    CefWindowInfo windowInfo;

    if (!params.parent)
    {
        windowInfo.SetAsWindowless(0);
    }
    else
    {
#if defined(OS_LINUX)
        const std::uintptr_t nativeValue =
            reinterpret_cast<std::uintptr_t>(params.parent);

        windowInfo.SetAsChild(
            static_cast<cef_window_handle_t>(nativeValue),
            CefRect(0, 0, 800, 600));
#else
        windowInfo.SetAsChild(
            reinterpret_cast<cef_window_handle_t>(params.parent),
            CefRect(0, 0, 800, 600));
#endif
    }

    trustedEntryUrl_ = params.source;

    if (params.source.compare(0, 6, "app://") == 0)
    {
        if (!params.appResources ||
            !params.appResources->contains(params.source))
            throw Error("app_resource_not_found",
                        "app entry is not in the packaged resource bundle");
        CefRequestContextSettings contextSettings;
        appRequestContext_ =
            CefRequestContext::CreateContext(contextSettings, nullptr);
        if (!appRequestContext_ ||
            !appRequestContext_->RegisterSchemeHandlerFactory(
                "app", "", new AppResourceFactory(params.appResources)))
            throw Error("app_scheme_registration_failed",
                        "Could not register isolated CEF app resource scheme");
    }
    else
        appRequestContext_ = nullptr;

    CefBrowserSettings settings;

    const bool started =
        CefBrowserHost::CreateBrowser(
            windowInfo,
            client_,
            params.source,
            settings,
            nullptr,
            appRequestContext_);

    if (!started)
        throw std::runtime_error("CefBrowserHost::CreateBrowser failed");
}

void CefBackend::destroy()
{
    CefRefPtr<CefBrowser> browser = browser_;

    if (!browser)
        return;

    runOnCefUi(
        [browser]() {
            browser->GetHost()->CloseBrowser(true);
        });
}

bool CefBackend::isCreated() const
{
    return browser_ != 0;
}

void CefBackend::load(const std::string& source)
{
    CefRefPtr<CefBrowser> browser = browser_;

    if (!browser)
        throw std::runtime_error("CEF browser is not created");

    runOnCefUi(
        [browser, source]() {
            browser->GetMainFrame()->LoadURL(source);
        });
}

void CefBackend::reload()
{
    CefRefPtr<CefBrowser> browser = browser_;

    if (!browser)
        throw std::runtime_error("CEF browser is not created");

    runOnCefUi(
        [browser]() {
            browser->Reload();
        });
}

void CefBackend::onAfterCreated(
    CefRefPtr<CefBrowser> browser)
{
    browser_ = browser;

    if (listener_)
        listener_->onBrowserCreated();
}

void CefBackend::onBeforeClose(
    CefRefPtr<CefBrowser> browser)
{
    browser_ = nullptr;

    if (listener_)
        listener_->onBrowserClosed();
}

void CefBackend::onLoadStart(
    CefRefPtr<CefFrame> frame)
{
    if (frame && frame->IsMain())
    {
        mainFrameReady_ = false;
        activeMainFrameIdentifier_.clear();
        documentSessionToken_.clear();
        ++navigation_;
        if (listener_)
            listener_->onLoadStarted(frame->GetURL().ToString());
    }
}

void CefBackend::onLoadEnd(
    CefRefPtr<CefFrame> frame)
{
    if (listener_ && frame && frame->IsMain())
    {
        const std::string url=frame->GetURL().ToString();
        const std::string frameIdentity=frame->GetIdentifier().ToString();
        if (navigation_ && !frameIdentity.empty() &&
            trustedEntryDocument(trustedEntryUrl_,url,true))
        {
            try { documentSessionToken_ = createDocumentSessionToken(); }
            catch (...) { documentSessionToken_.clear(); }
            if (!documentSessionToken_.empty()) mainFrameReady_=true;
            activeMainFrameIdentifier_=frameIdentity;
            VerifiedFrameContext ctx;
            // Numeric slot remains private to the native backend. The exact
            // engine string identifier is verified before accepting requests.
            ctx.frameId=1;
            ctx.navigation=navigation_;
            ctx.engineVerified=true;
            ctx.mainFrame=true;
            ctx.trustedContent=true;
            if (mainFrameReady_) {
                listener_->onTrustedFrameActivated(ctx);
                CefRefPtr<CefProcessMessage> offer =
                    CefProcessMessage::Create("nativeweb.document.session");
                offer->GetArgumentList()->SetString(0,documentSessionToken_);
                frame->SendProcessMessage(PID_RENDERER,offer);
                std::cerr << "cef-document-session-issued" << std::endl;
            }
        }
        listener_->onLoadFinished(url);
    }
}

bool CefBackend::onProcessMessage(
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefProcessMessage> message)
{
    const CefString messageName =
        message->GetName();

    CefRefPtr<CefListValue> list =
        message->GetArgumentList();

    if (messageName == "nativeweb.cancel")
    {
        // Authenticate from the engine frame, live document and session;
        // a renderer cannot cancel another document's request ID.
        if (frame && mainFrameReady_ &&
            frame->IsMain() &&
            frame->GetIdentifier().ToString()==activeMainFrameIdentifier_ &&
            trustedEntryDocument(trustedEntryUrl_,
                                 frame->GetURL().ToString(),true) &&
            matchesDocumentSessionToken(
                list->GetString(1).ToString(),documentSessionToken_))
        {
            const RequestId id=stringToId(list->GetString(0).ToString());
            if(listener_) listener_->onBridgeMessage(makeCancelMessage(id));
        }
        return true;
    }

    if (messageName == "nativeweb.call.response")
    {
        // Renderer -> native responses are untrusted IPC too. A child
        // frame/remote page cannot forge an answer to WebView::execute().
        if (!frame || !mainFrameReady_ ||
            frame->GetIdentifier().ToString()!=activeMainFrameIdentifier_ ||
            !trustedEntryDocument(
                trustedEntryUrl_, frame->GetURL().ToString(), frame->IsMain()) ||
            !matchesDocumentSessionToken(
                list->GetString(4).ToString(),documentSessionToken_))
            return true;

        const RequestId requestId =
            stringToId(list->GetString(0).ToString());

        if (listener_)
        {
            if (list->GetBool(1))
            {
                listener_->onBridgeMessage(
                    makeResponseMessage(
                        requestId,
                        cefValueToAny(
                            list->GetValue(2))));
            }
            else
            {
                listener_->onBridgeMessage(
                    makeErrorMessage(
                        requestId,
                        Error(
                            list->GetString(2).ToString(),
                            list->GetString(3).ToString())));
            }
        }

        return true;
    }

    if (messageName == "nativeweb.client.event")
    {
        // The same engine-supplied main-frame and document-session checks
        // apply to one-way messages, including same-URL reloads.
        if (!frame || !frame->IsMain() || !mainFrameReady_ ||
            frame->GetIdentifier().ToString()!=activeMainFrameIdentifier_ ||
            !trustedEntryDocument(trustedEntryUrl_,
                                  frame->GetURL().ToString(),true) ||
            !matchesDocumentSessionToken(
                list->GetString(3).ToString(),documentSessionToken_))
            return true;
        const std::string channel = list->GetString(0).ToString();
        if (channel.empty() || channel.size()>256)
            return true;
        if (listener_)
        {
            VerifiedFrameContext ctx;
            ctx.frameId=1; // Authenticated engine main-frame slot.
            ctx.navigation=navigation_;
            ctx.engineVerified=mainFrameReady_ &&
                !activeMainFrameIdentifier_.empty() &&
                frame->GetIdentifier().ToString()==activeMainFrameIdentifier_;
            ctx.mainFrame=frame->IsMain();
            ctx.trustedContent=trustedEntryDocument(
                trustedEntryUrl_,frame->GetURL().ToString(),ctx.mainFrame);
            listener_->onVerifiedBridgeMessage(
                makeEventMessage(channel,cefValueToAny(list->GetValue(1))),ctx);
        }
        return true;
    }

    if (messageName != "nativeweb.request")
        return false;

    const RequestId requestId =
        stringToId(list->GetString(0).ToString());

    // The sender's CefFrame is engine-supplied, NOT request-payload data.
    // Only the explicitly hosted local entry document gets bridge access.
    if (!frame || !trustedEntryDocument(
            trustedEntryUrl_, frame->GetURL().ToString(), frame->IsMain()))
    {
        // Return denial to the *originating* frame, not the main page.
        // It must not reach BridgeRuntime, and must not strand a child's
        // Promise waiting on a response sent to another document.
        if (frame)
        {
            CefRefPtr<CefProcessMessage> denied =
                CefProcessMessage::Create("nativeweb.response");
            CefRefPtr<CefListValue> deniedArgs = denied->GetArgumentList();
            deniedArgs->SetString(0, idToString(requestId));
            deniedArgs->SetBool(1, false);
            deniedArgs->SetString(2, "permission_denied");
            deniedArgs->SetString(3, "Native APIs are unavailable in this frame");
            frame->SendProcessMessage(PID_RENDERER, denied);
        }
        return true;
    }

    // Do not emit a rejection to the new main page: a stale document's
    // request ID can collide with a new document's Promise request ID.
    if (!mainFrameReady_ ||
        frame->GetIdentifier().ToString()!=activeMainFrameIdentifier_ ||
        !matchesDocumentSessionToken(
            list->GetString(3).ToString(),documentSessionToken_))
        return true;

    const std::string method =
        list->GetString(1).ToString();

    VariantList args;
    CefRefPtr<CefListValue> cefArgs =
        list->GetList(2);

    const std::size_t count =
        cefArgs ? cefArgs->GetSize() : 0;

    args.reserve(count);

    for (std::size_t i = 0; i < count; ++i)
        args.push_back(
            cefValueToAny(cefArgs->GetValue(i)));

    if (listener_)
    {
        VerifiedFrameContext ctx;
        ctx.frameId=1;
        ctx.navigation=navigation_;
        ctx.engineVerified=mainFrameReady_ &&
            !activeMainFrameIdentifier_.empty() &&
            frame->GetIdentifier().ToString()==activeMainFrameIdentifier_;
        ctx.mainFrame=frame->IsMain();
        ctx.trustedContent=trustedEntryDocument(
            trustedEntryUrl_,frame->GetURL().ToString(),ctx.mainFrame);
        listener_->onVerifiedBridgeMessage(
            makeRequestMessage(requestId,method,args),ctx);
    }

    return true;
}

void CefBackend::postBridgeMessage(const Any& message)
{
    CefRefPtr<CefBrowser> browser = browser_;

    if (!browser)
        return;

    runOnCefUi(
        [browser, message]() {
            sendBridgeMessageToBrowser(
                browser,
                message);
        });
}

void registerCefBackendFactory()
{
    registerBrowserBackendFactory(
        Engine::Cef,
        []() -> std::unique_ptr<BrowserBackend>
        {
            return std::unique_ptr<BrowserBackend>(
                new CefBackend());
        });
}

} // namespace detail
} // namespace nativeweb
