#include "browser/webview2/webview2_backend.hpp"

#include "browser/common/backend_registry.hpp"
#include "browser/common/trusted_document.hpp"
#include "browser/common/document_session_token.hpp"
#include "core/bridge_message.hpp"
#include "browser/webview2/webview2_bridge_script.hpp"
#include "core/json_codec.hpp"
#include "nativeweb/error.hpp"
#include "nativeweb/app_resources.hpp"
#include <WebView2EnvironmentOptions.h>
#include <objidl.h>

#include <memory>
#include <psapi.h>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <wrl/event.h>

namespace nativeweb {
namespace detail {

namespace {

const wchar_t* kDispatchWindowClass =
    L"NativeWeb.WebView2.Dispatch";

const UINT kDispatchTaskMessage =
    WM_APP + 0x4e57;

void checkHresult(
    HRESULT result,
    const char* operation)
{
    if (SUCCEEDED(result))
        return;

    std::ostringstream message;
    message
        << operation
        << " failed (HRESULT=0x"
        << std::hex
        << static_cast<unsigned long>(result)
        << ")";

    throw Error(
        "webview2_error",
        message.str());
}

} // namespace

WebView2Backend::WebView2Backend()
    : listener_(0),
      parent_(0),
      dispatchWindow_(0),
      uiThreadId_(0),
      comInitialized_(false),
      created_(false),
      closing_(false),
      webResourceRequestedRegistered_(false),
      webMessageRegistered_(false),
      navigationStartingRegistered_(false),
      navigationCompletedRegistered_(false),
      processFailedRegistered_(false)
{
    webResourceRequestedToken_.value=0;
    webMessageToken_.value = 0;
    navigationStartingToken_.value = 0;
    navigationCompletedToken_.value = 0;
    processFailedToken_.value=0;
}

WebView2Backend::~WebView2Backend()
{
    if (
        uiThreadId_ != 0 &&
        GetCurrentThreadId() == uiThreadId_)
    {
        destroyOnUi();
        destroyDispatchWindow();

        if (comInitialized_)
        {
            CoUninitialize();
            comInitialized_ = false;
        }
    }
}

const char* WebView2Backend::engineName() const
{
    return "webview2";
}

Engine WebView2Backend::engine() const
{
    return Engine::WebView2;
}

Capabilities WebView2Backend::capabilities() const
{
    return Capabilities(
        static_cast<std::uint64_t>(
            Capability::Binary) |
        static_cast<std::uint64_t>(
            Capability::Events) |
        static_cast<std::uint64_t>(
            Capability::AsyncJavaScript));
}

void WebView2Backend::setListener(
    BrowserBackendListener* listener)
{
    listener_ = listener;
}

void WebView2Backend::requireUiThread() const
{
    if (
        uiThreadId_ != 0 &&
        GetCurrentThreadId() != uiThreadId_)
    {
        throw Error(
            "webview2_wrong_thread",
            "WebView2 operation must run on its UI thread");
    }
}

void WebView2Backend::setIsolatedUserDataFolderForTest(
    const std::wstring& folder)
{
    requireUiThread();
    if(created_.load() || folder.empty())
        throw Error("invalid_test_profile","Isolated test folder must be set before create");
    isolatedUserDataFolderForTest_=folder;
}

void WebView2Backend::create(
    const BrowserCreateParams& params)
{
    if (created_.load())
    {
        throw Error(
            "webview_already_created",
            "NativeWeb WebView2 backend is already created");
    }
    if (params.source.compare(0, 6, "app://") == 0 &&
        (!params.appResources ||
         !params.appResources->contains(params.source)))
        throw Error("app_resource_not_found",
                    "app:// entry is not in the packaged WebView2 resource bundle");


    if (
        !params.parent ||
        !IsWindow(
            reinterpret_cast<HWND>(
                params.parent)))
    {
        throw Error(
            "invalid_parent_window",
            "WebView2 requires a valid Win32 parent HWND");
    }

    uiThreadId_ = GetCurrentThreadId();
    parent_ =
        reinterpret_cast<HWND>(
            params.parent);
    source_ = params.source;
    trustedEntryUrl_ = params.source;
    appResources_ = params.appResources;
    closing_.store(false);

    const HRESULT comResult =
        CoInitializeEx(
            0,
            COINIT_APARTMENTTHREADED);

    if (comResult == RPC_E_CHANGED_MODE)
    {
        throw Error(
            "webview2_com_apartment",
            "WebView2 UI thread must use an STA COM apartment");
    }

    checkHresult(
        comResult,
        "CoInitializeEx");

    comInitialized_ = true;
    createDispatchWindow();

    // An authority-bearing secure app:// origin must be registered BEFORE
    // the WebView2 Environment is created, including ordinary file:// hosts
    // (all environments sharing browser processes must agree on schemes).
    auto environmentOptions =
        Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions4> options4;
    checkHresult(environmentOptions.As(&options4),
                 "Query ICoreWebView2EnvironmentOptions4");
    auto appScheme =
        Microsoft::WRL::Make<CoreWebView2CustomSchemeRegistration>(L"app");
    checkHresult(appScheme->put_HasAuthorityComponent(TRUE),
                 "app:// authority option");
    checkHresult(appScheme->put_TreatAsSecure(TRUE),
                 "app:// secure option");
    ICoreWebView2CustomSchemeRegistration* schemes[] = {appScheme.Get()};
    checkHresult(options4->SetCustomSchemeRegistrations(1, schemes),
                 "SetCustomSchemeRegistrations");

    CreateState state;

    const HRESULT createResult =
        CreateCoreWebView2EnvironmentWithOptions(
            0,
            isolatedUserDataFolderForTest_.empty() ? 0 :
                isolatedUserDataFolderForTest_.c_str(),
            environmentOptions.Get(),
            Microsoft::WRL::Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, &state](
                    HRESULT result,
                    ICoreWebView2Environment* environment)
                    -> HRESULT
                {
                    if (FAILED(result) || !environment)
                    {
                        state.result =
                            FAILED(result)
                                ? result
                                : E_FAIL;
                        state.done = true;
                        return S_OK;
                    }

                    environment_ = environment;

                    const HRESULT controllerResult =
                        environment_->CreateCoreWebView2Controller(
                            parent_,
                            Microsoft::WRL::Callback<
                                ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [this, &state](
                                    HRESULT result,
                                    ICoreWebView2Controller* controller)
                                    -> HRESULT
                                {
                                    if (FAILED(result) || !controller)
                                    {
                                        state.result =
                                            FAILED(result)
                                                ? result
                                                : E_FAIL;
                                        state.done = true;
                                        return S_OK;
                                    }

                                    try
                                    {
                                        finishControllerCreation(
                                            controller,
                                            &state);
                                    }
                                    catch (...)
                                    {
                                        state.result = E_FAIL;
                                        state.done = true;
                                    }

                                    return S_OK;
                                }).Get());

                    if (FAILED(controllerResult))
                    {
                        state.result = controllerResult;
                        state.done = true;
                    }

                    return S_OK;
                }).Get());

    if (FAILED(createResult))
    {
        state.result = createResult;
        state.done = true;
    }

    if (!pumpUntil(state, 30000))
    {
        destroyOnUi();
        throw Error(
            "webview2_timeout",
            "Timed out creating WebView2");
    }

    if (FAILED(state.result))
    {
        const HRESULT failure =
            state.result;

        destroyOnUi();

        checkHresult(
            failure,
            "CreateCoreWebView2EnvironmentWithOptions");
    }
}

void WebView2Backend::finishControllerCreation(
    ICoreWebView2Controller* controller,
    CreateState* state)
{
    controller_ = controller;

    checkHresult(
        controller_->get_CoreWebView2(
            &webview_),
        "ICoreWebView2Controller::get_CoreWebView2");

    RECT bounds;
    GetClientRect(parent_, &bounds);

    checkHresult(
        controller_->put_Bounds(bounds),
        "ICoreWebView2Controller::put_Bounds");

    Microsoft::WRL::ComPtr<
        ICoreWebView2Settings> settings;

    checkHresult(
        webview_->get_Settings(&settings),
        "ICoreWebView2::get_Settings");

    if (settings)
    {
        checkHresult(
            settings->put_IsWebMessageEnabled(TRUE),
            "ICoreWebView2Settings::put_IsWebMessageEnabled");
    }

    checkHresult(
        installHandlers(),
        "install WebView2 handlers");

    checkHresult(
        installBridgeScript(state),
        "ICoreWebView2::AddScriptToExecuteOnDocumentCreated");
}

HRESULT WebView2Backend::installHandlers()
{
    // Explicit app:// resource responses: no file:// fallback, CORS wildcard,
    // or filesystem access. Filter covers images, CSS, document and fetch.
    HRESULT result = webview_->AddWebResourceRequestedFilter(
        L"app://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    if (FAILED(result)) return result;
    result = webview_->add_WebResourceRequested(
        Microsoft::WRL::Callback<
            ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2* sender,
                   ICoreWebView2WebResourceRequestedEventArgs* args)
                -> HRESULT
            {
                if (!args || !environment_) return E_INVALIDARG;
                Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
                HRESULT r=args->get_Request(&request);
                if (FAILED(r) || !request) return FAILED(r)?r:E_FAIL;
                LPWSTR uri=0;
                r=request->get_Uri(&uri);
                if (FAILED(r) || !uri) return FAILED(r)?r:E_FAIL;
                const std::string url=wideToUtf8(uri);
                CoTaskMemFree(uri);
                if (url.compare(0,6,"app://") != 0) return S_OK;

                AppResource resource;
                const bool found=appResources_ &&
                    appResources_->find(url, &resource);
                const std::string body=found?resource.bytes:"Not Found";
                const std::string mime=found?resource.mimeType:"text/plain";

                Microsoft::WRL::ComPtr<IStream> stream;
                r=CreateStreamOnHGlobal(nullptr, TRUE, &stream);
                if (FAILED(r)) return r;
                if (!body.empty())
                {
                    ULONG written=0;
                    r=stream->Write(body.data(),
                                    static_cast<ULONG>(body.size()), &written);
                    if (FAILED(r) || written!=body.size())
                        return FAILED(r)?r:E_FAIL;
                }
                LARGE_INTEGER zero;
                zero.QuadPart=0;
                r=stream->Seek(zero,STREAM_SEEK_SET,nullptr);
                if (FAILED(r)) return r;

                const std::wstring headers =
                    L"Content-Type: " + utf8ToWide(mime) +
                    L"\r\nX-Content-Type-Options: nosniff"
                    L"\r\nCache-Control: no-store";
                Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
                r=environment_->CreateWebResourceResponse(
                    stream.Get(),found?200:404,
                    found?L"OK":L"Not Found",headers.c_str(),&response);
                if (FAILED(r)) return r;
                return args->put_Response(response.Get());
            }).Get(),
        &webResourceRequestedToken_);
    if (FAILED(result)) return result;
    webResourceRequestedRegistered_=true;

    result = webview_->add_WebMessageReceived(
            Microsoft::WRL::Callback<
                ICoreWebView2WebMessageReceivedEventHandler>(
                [this](
                    ICoreWebView2*,
                    ICoreWebView2WebMessageReceivedEventArgs* args)
                    -> HRESULT
                {
                    if (!args || !listener_)
                        return S_OK;

                    LPWSTR json = 0;

                    const HRESULT jsonResult =
                        args->get_WebMessageAsJson(
                            &json);

                    if (FAILED(jsonResult) || !json)
                        return S_OK;

                    try
                    {
                        const std::string utf8 =
                            wideToUtf8(json);

                        CoTaskMemFree(json);
                        json = 0;

                        const Any message = jsonToAny(utf8);
                        const VariantDict& fields=AnyCast<const VariantDict&>(message);
                        const BridgeMessage parsed = parseBridgeMessage(message);
                        {
                            // ALL renderer-originated envelope types must
                            // come from the trusted page, including responses
                            // to C++ -> JS calls (not just JS requests).
                            // Use engine event source, not JS fields.
                            LPWSTR sender = 0;
                            LPWSTR topLevel = 0;
                            const HRESULT senderResult = args->get_Source(&sender);
                            const HRESULT topResult = webview_->get_Source(&topLevel);
                            const std::string senderUrl =
                                SUCCEEDED(senderResult) && sender
                                    ? wideToUtf8(sender) : std::string();
                            const std::string mainUrl =
                                SUCCEEDED(topResult) && topLevel
                                    ? wideToUtf8(topLevel) : std::string();
                            if (sender) CoTaskMemFree(sender);
                            if (topLevel) CoTaskMemFree(topLevel);

                            if (!trustedEntryDocument(
                                    trustedEntryUrl_, senderUrl, true) ||
                                senderUrl != mainUrl)
                            {
                                // WebView2::PostWebMessageAsJson targets the
                                // top-level document. Never send a child
                                // frame's error there: its request id could
                                // collide with a legitimate main-frame call.
                                if (parsed.type == BridgeMessageType::Request &&
                                    senderUrl == mainUrl)
                                    postBridgeMessageOnUi(
                                        makeErrorMessage(
                                            parsed.requestId,
                                            Error("permission_denied",
                                                  "This document is not trusted to invoke native APIs")));
                                // Never forward forged native call responses,
                                // errors or events from untrusted documents.
                                return S_OK;
                            }
                        }
                        const VariantDict::const_iterator tokenField=fields.find("session");
                        const std::string token=tokenField!=fields.end()
                            ? AnyCast<std::string>(tokenField->second) : std::string();
                        if (!mainFrameReady_ ||
                            !matchesDocumentSessionToken(token,documentSessionToken_))
                            return S_OK; // stale same-URL document or missing token
                        if(parsed.type==BridgeMessageType::Event &&
                           (parsed.eventName.empty() || parsed.eventName.size()>256))
                            return S_OK;
                        if(parsed.type==BridgeMessageType::Request ||
                           parsed.type==BridgeMessageType::Event)
                        {
                            // Independently authenticate one-way messages.
                            VerifiedFrameContext ctx;
                            ctx.frameId=1; // engine top-level event slot
                            ctx.navigation=navigation_;
                            ctx.engineVerified=mainFrameReady_ && navigation_!=0;
                            ctx.mainFrame=true;
                            ctx.trustedContent=true; // checked above using engine URLs
                            listener_->onVerifiedBridgeMessage(message,ctx);
                        }
                        else
                            listener_->onBridgeMessage(message);
                    }
                    catch (...)
                    {
                        if (json)
                            CoTaskMemFree(json);
                    }

                    return S_OK;
                }).Get(),
            &webMessageToken_);

    if (FAILED(result))
        return result;

    webMessageRegistered_ = true;

    result =
        webview_->add_NavigationStarting(
            Microsoft::WRL::Callback<
                ICoreWebView2NavigationStartingEventHandler>(
                [this](
                    ICoreWebView2*,
                    ICoreWebView2NavigationStartingEventArgs* args)
                    -> HRESULT
                {
                    if (!args)
                        return S_OK;

                    LPWSTR uri = 0;

                    if (
                        SUCCEEDED(
                            args->get_Uri(&uri)) &&
                        uri)
                    {
                        notifyLoadStarted(uri);
                        CoTaskMemFree(uri);
                    }

                    return S_OK;
                }).Get(),
            &navigationStartingToken_);

    if (FAILED(result))
        return result;

    navigationStartingRegistered_ = true;

    result =
        webview_->add_NavigationCompleted(
            Microsoft::WRL::Callback<
                ICoreWebView2NavigationCompletedEventHandler>(
                [this](
                    ICoreWebView2*,
                    ICoreWebView2NavigationCompletedEventArgs*)
                    -> HRESULT
                {
                    notifyLoadFinished();
                    return S_OK;
                }).Get(),
            &navigationCompletedToken_);

    if (FAILED(result))
        return result;

    navigationCompletedRegistered_ = true;

    result = webview_->add_ProcessFailed(
        Microsoft::WRL::Callback<ICoreWebView2ProcessFailedEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2ProcessFailedEventArgs* args)->HRESULT
            {
                if(!args) return S_OK;
                COREWEBVIEW2_PROCESS_FAILED_KIND kind;
                if(FAILED(args->get_ProcessFailedKind(&kind))) return S_OK;
                if(kind!=COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED &&
                   kind!=COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED)
                    return S_OK;
                // Revoke all renderer grants BEFORE any host recovery.
                mainFrameReady_=false;
                documentSessionToken_.clear();
                sharedProbePending_.clear();
                sharedProbePendingBytes_=0;
                sharedProbeCumulativeBytes_=0;
                ++navigation_;
                if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED)
                    ++rendererFailures_;
                else
                    ++browserProcessFailures_;
                // Do not reenter WebView2 create/reload from this event.
                // The host must choose explicit renderer reload or recreate.
                return S_OK;
            }).Get(),&processFailedToken_);
    if(FAILED(result)) return result;
    processFailedRegistered_=true;
    return S_OK;
}

HRESULT WebView2Backend::installBridgeScript(
    CreateState* state)
{
    return webview_->AddScriptToExecuteOnDocumentCreated(
        kWebView2BridgeScript,
        Microsoft::WRL::Callback<
            ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, state](
                HRESULT result,
                LPCWSTR)
                -> HRESULT
            {
                if (FAILED(result))
                {
                    state->result = result;
                    state->done = true;
                    return S_OK;
                }

                created_.store(true);

                if (listener_)
                    listener_->onBrowserCreated();

                const std::wstring url =
                    utf8ToWide(
                        source_.empty()
                            ? std::string("about:blank")
                            : source_);

                state->result =
                    webview_->Navigate(
                        url.c_str());

                state->done = true;
                return S_OK;
            }).Get());
}

bool WebView2Backend::pumpUntil(
    CreateState& state,
    DWORD timeoutMs)
{
    const ULONGLONG started =
        GetTickCount64();

    while (!state.done)
    {
        MSG message;

        while (
            PeekMessageW(
                &message,
                0,
                0,
                0,
                PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                PostQuitMessage(
                    static_cast<int>(
                        message.wParam));

                state.result = E_ABORT;
                state.done = true;
                break;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (state.done)
            break;

        if (
            GetTickCount64() - started >=
            timeoutMs)
        {
            return false;
        }

        MsgWaitForMultipleObjectsEx(
            0,
            0,
            25,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
    }

    return true;
}

void WebView2Backend::destroy()
{
    if (uiThreadId_ == 0)
        return;

    if (GetCurrentThreadId() == uiThreadId_)
    {
        destroyOnUi();
        return;
    }

    postToUi(
        [this]() {
            destroyOnUi();
        });
}

void WebView2Backend::destroyOnUi()
{
    if (closing_.exchange(true))
        return;

    sharedProbePending_.clear();
    sharedProbePendingBytes_=0;
    sharedProbeCumulativeBytes_=0;
    const bool wasCreated =
        created_.exchange(false);

    if (webview_)
    {
        if (webResourceRequestedRegistered_)
        {
            webview_->remove_WebResourceRequested(webResourceRequestedToken_);
            webResourceRequestedRegistered_ = false;
        }
        if (webMessageRegistered_)
        {
            webview_->remove_WebMessageReceived(
                webMessageToken_);
            webMessageRegistered_ = false;
        }

        if (navigationStartingRegistered_)
        {
            webview_->remove_NavigationStarting(
                navigationStartingToken_);
            navigationStartingRegistered_ = false;
        }

        if (navigationCompletedRegistered_)
        {
            webview_->remove_NavigationCompleted(
                navigationCompletedToken_);
            navigationCompletedRegistered_ = false;
        }
        if (processFailedRegistered_)
        {
            webview_->remove_ProcessFailed(processFailedToken_);
            processFailedRegistered_=false;
        }
    }

    if (controller_)
        controller_->Close();

    webview_.Reset();
    controller_.Reset();
    environment_.Reset();

    if (wasCreated && listener_)
        listener_->onBrowserClosed();
}

bool WebView2Backend::isCreated() const
{
    return created_.load();
}

void WebView2Backend::load(
    const std::string& source)
{
    requireUiThread();

    if (!created_.load() || !webview_)
    {
        throw Error(
            "webview_not_created",
            "WebView2 is not created");
    }

    navigateOnUi(source);
}

void WebView2Backend::navigateOnUi(
    const std::string& source)
{
    source_ = source;

    const std::wstring wide =
        utf8ToWide(source);

    checkHresult(
        webview_->Navigate(
            wide.c_str()),
        "ICoreWebView2::Navigate");
}

void WebView2Backend::reload()
{
    requireUiThread();

    if (!created_.load() || !webview_)
    {
        throw Error(
            "webview_not_created",
            "WebView2 is not created");
    }

    checkHresult(
        webview_->Reload(),
        "ICoreWebView2::Reload");
}

void WebView2Backend::postBridgeMessage(
    const Any& message)
{
    if (!created_.load())
        return;

    if (GetCurrentThreadId() == uiThreadId_)
    {
        postBridgeMessageOnUi(message);
        return;
    }

    postToUi(
        [this, message]() {
            postBridgeMessageOnUi(message);
        });
}

void WebView2Backend::postBridgeMessageOnUi(
    const Any& message)
{
    if (!created_.load() || !webview_)
        return;

    const std::wstring json =
        utf8ToWide(
            anyToJsonCompactBinary(message));

    checkHresult(
        webview_->PostWebMessageAsJson(
            json.c_str()),
        "ICoreWebView2::PostWebMessageAsJson");
}

bool WebView2Backend::postReadOnlySharedBufferProbe(
    const Binary& bytes, int requestId)
{
    requireUiThread();
    // The sender must independently authorize the triggering JS message via
    // onVerifiedBridgeMessage. This function then re-checks active document.
    if (!webview_ || !environment_ || !mainFrameReady_ ||
        closing_.load() || !created_.load() || requestId < 0 ||
        bytes.empty() || bytes.size() > 1024u*1024u ||
        sharedProbePending_.count(requestId) != 0u ||
        sharedProbePending_.size() >= kSharedProbeMaxPendingRequests ||
        bytes.size() > kSharedProbeMaxPendingBytes-sharedProbePendingBytes_ ||
        bytes.size() > kSharedProbeMaxDocumentBytes-sharedProbeCumulativeBytes_)
        return false;

    LPWSTR rawUrl = nullptr;
    if (FAILED(webview_->get_Source(&rawUrl)) || !rawUrl)
        return false;
    const std::string current = wideToUtf8(rawUrl);
    CoTaskMemFree(rawUrl);
    if (!trustedEntryDocument(trustedEntryUrl_, current, true))
        return false;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment12> environment12;
    Microsoft::WRL::ComPtr<ICoreWebView2_17> view17;
    if (FAILED(environment_.As(&environment12)) ||
        FAILED(webview_.As(&view17)))
        return false;

    Microsoft::WRL::ComPtr<ICoreWebView2SharedBuffer> shared;
    if (FAILED(environment12->CreateSharedBuffer(
            static_cast<UINT64>(bytes.size()), &shared)) || !shared)
        return false;

    BYTE* memory = nullptr;
    if (FAILED(shared->get_Buffer(&memory)) || !memory)
    {
        shared->Close();
        return false;
    }
    std::memcpy(memory, bytes.data(), bytes.size());
    // Keep control metadata tiny. Never expose document-session tokens via
    // SharedBuffer additionalData (or any renderer-supplied ID authority).
    std::string grant;
    try { grant = createDocumentSessionToken(); }
    catch (...) { shared->Close(); return false; } // fail closed

    std::ostringstream metadata;
    metadata << "{\"type\":\"xytron.shared.probe\",\"id\":"
             << requestId << ",\"size\":" << bytes.size()
             << ",\"grant\":\"" << grant << "\"}";
    const std::wstring data = utf8ToWide(metadata.str());
    const HRESULT result = view17->PostSharedBufferToScript(
        shared.Get(), COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_ONLY,
        data.c_str());
    // Native may close after Post; JS must independently releaseBuffer().
    shared->Close();
    if (SUCCEEDED(result)) {
        ProbeGrant pending;
        pending.bytes=bytes.size();
        pending.nonce=grant;
        sharedProbePending_[requestId]=pending;
        sharedProbePendingBytes_+=bytes.size();
        sharedProbeCumulativeBytes_+=bytes.size();
    }
    return SUCCEEDED(result);
}

bool WebView2Backend::acknowledgeReadOnlySharedBufferProbe(
    int requestId, const std::string& grant)
{
    requireUiThread();
    if (!created_.load() || !mainFrameReady_ || closing_.load())
        return false;
    const std::map<int,ProbeGrant>::iterator it =
        sharedProbePending_.find(requestId);
    if (it == sharedProbePending_.end() ||
        !matchesDocumentSessionToken(grant,it->second.nonce))
        return false;
    sharedProbePendingBytes_-=it->second.bytes;
    sharedProbePending_.erase(it);
    return true;
}

std::size_t WebView2Backend::pendingReadOnlySharedBufferBytesForTest() const
{
    requireUiThread();
    return sharedProbePendingBytes_;
}

std::size_t WebView2Backend::cumulativeReadOnlySharedBufferBytesForTest() const
{
    requireUiThread();
    return sharedProbeCumulativeBytes_;
}

bool WebView2Backend::requestRendererCrashForTest()
{
    requireUiThread();
    if(!webview_ || !created_.load() || closing_.load() || !mainFrameReady_)
        return false;
    // Test-only DevTools command: a successful HRESULT means the command was
    // submitted, not that a ProcessFailed callback was observed.
    return SUCCEEDED(webview_->CallDevToolsProtocolMethod(
        L"Page.crash", L"{}",
        Microsoft::WRL::Callback<
            ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [](HRESULT, LPCWSTR)->HRESULT { return S_OK; }).Get()));
}

DWORD WebView2Backend::browserProcessIdForTest() const
{
    requireUiThread();
    UINT32 pid=0;
    if(webview_) (void)webview_->get_BrowserProcessId(&pid);
    return static_cast<DWORD>(pid);
}

bool WebView2Backend::processGroupMemoryForTest(
    ProcessMemorySnapshot* result) const
{
    requireUiThread();
    if (!result || !environment_ || closing_.load())
        return false;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment8> env8;
    if (FAILED(environment_.As(&env8)))
        return false;
    Microsoft::WRL::ComPtr<ICoreWebView2ProcessInfoCollection> list;
    if (FAILED(env8->GetProcessInfos(&list)) || !list)
        return false;
    UINT count=0;
    if (FAILED(list->get_Count(&count)))
        return false;
    ProcessMemorySnapshot reading;
    reading.processCount=count;
    for (UINT i=0;i<count;++i)
    {
        Microsoft::WRL::ComPtr<ICoreWebView2ProcessInfo> info;
        if (FAILED(list->GetValueAtIndex(i,&info)) || !info)
            return false;
        INT32 pid=0;
        COREWEBVIEW2_PROCESS_KIND kind;
        if (FAILED(info->get_ProcessId(&pid)) ||
            FAILED(info->get_Kind(&kind)))
            return false;
        if (kind==COREWEBVIEW2_PROCESS_KIND_RENDERER)
            ++reading.rendererCount;
        // Processes may exit between enumerating and sampling. Report
        // counts separately instead of fabricating zero-byte readings.
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                   FALSE,static_cast<DWORD>(pid));
        if (!process) continue;
        PROCESS_MEMORY_COUNTERS_EX pmc={};
        pmc.cb=sizeof(pmc);
        const BOOL ok=GetProcessMemoryInfo(
            process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
            sizeof(pmc));
        CloseHandle(process);
        if (!ok) continue;
        ++reading.sampledCount;
        reading.totalPrivateBytes+=pmc.PrivateUsage;
        reading.totalWorkingSetBytes+=pmc.WorkingSetSize;
        if (kind==COREWEBVIEW2_PROCESS_KIND_RENDERER)
            reading.rendererPrivateBytes+=pmc.PrivateUsage;
    }
    // Do not present an empty or unsupported environment as zero usage.
    if (!reading.sampledCount)
        return false;
    *result=reading;
    return true;
}

unsigned int WebView2Backend::rendererFailuresForTest() const
{
    requireUiThread();
    return rendererFailures_;
}

unsigned int WebView2Backend::browserProcessFailuresForTest() const
{
    requireUiThread();
    return browserProcessFailures_;
}

void WebView2Backend::notifyLoadStarted(
    const std::wstring& source)
{
    mainFrameReady_=false;
    documentSessionToken_.clear();
    sharedProbePending_.clear();
    sharedProbePendingBytes_=0;
    sharedProbeCumulativeBytes_=0;
    ++navigation_;
    try { documentSessionToken_=createDocumentSessionToken(); }
    catch (...) { documentSessionToken_.clear(); } // fail closed
    if (listener_)
    {
        listener_->onLoadStarted(
            wideToUtf8(
                source.c_str()));
    }
}

void WebView2Backend::notifyLoadFinished()
{
    if (!listener_ || !webview_)
        return;

    LPWSTR source = 0;

    if (
        SUCCEEDED(
            webview_->get_Source(
                &source)) &&
        source)
    {
        const std::string url=wideToUtf8(source);
        if(navigation_ && !documentSessionToken_.empty() &&
           trustedEntryDocument(trustedEntryUrl_,url,true))
        {
            mainFrameReady_=true;
            VerifiedFrameContext ctx;
            ctx.frameId=1;
            ctx.navigation=navigation_;
            ctx.engineVerified=true;
            ctx.mainFrame=true;
            ctx.trustedContent=true;
            listener_->onTrustedFrameActivated(ctx);
            VariantDict handshake;
            handshake["type"]=std::string("nativeweb_session");
            handshake["token"]=documentSessionToken_;
            postBridgeMessageOnUi(Any(handshake));
        }
        listener_->onLoadFinished(url);
        CoTaskMemFree(source);
    }
}

void WebView2Backend::createDispatchWindow()
{
    if (dispatchWindow_)
        return;

    WNDCLASSW windowClass;
    ZeroMemory(
        &windowClass,
        sizeof(windowClass));

    windowClass.lpfnWndProc =
        &WebView2Backend::dispatchWindowProc;
    windowClass.hInstance =
        GetModuleHandleW(0);
    windowClass.lpszClassName =
        kDispatchWindowClass;

    if (!RegisterClassW(&windowClass))
    {
        const DWORD error =
            GetLastError();

        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            throw Error(
                "webview2_dispatch_window",
                "Failed to register WebView2 dispatch window");
        }
    }

    dispatchWindow_ =
        CreateWindowExW(
            0,
            kDispatchWindowClass,
            L"",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            0,
            GetModuleHandleW(0),
            this);

    if (!dispatchWindow_)
    {
        throw Error(
            "webview2_dispatch_window",
            "Failed to create WebView2 dispatch window");
    }
}

void WebView2Backend::destroyDispatchWindow()
{
    if (dispatchWindow_)
    {
        DestroyWindow(dispatchWindow_);
        dispatchWindow_ = 0;
    }
}

bool WebView2Backend::postToUi(
    const std::function<void()>& function)
{
    if (!function || !dispatchWindow_)
        return false;

    if (GetCurrentThreadId() == uiThreadId_)
    {
        function();
        return true;
    }

    std::unique_ptr<DispatchTask> task(
        new DispatchTask(function));

    if (!PostMessageW(
            dispatchWindow_,
            kDispatchTaskMessage,
            0,
            reinterpret_cast<LPARAM>(
                task.get())))
    {
        return false;
    }

    task.release();
    return true;
}

LRESULT CALLBACK WebView2Backend::dispatchWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    WebView2Backend* backend =
        reinterpret_cast<WebView2Backend*>(
            GetWindowLongPtrW(
                window,
                GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        CREATESTRUCTW* create =
            reinterpret_cast<CREATESTRUCTW*>(
                lParam);

        backend =
            static_cast<WebView2Backend*>(
                create->lpCreateParams);

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(
                backend));
    }

    if (message == kDispatchTaskMessage)
    {
        std::unique_ptr<DispatchTask> task(
            reinterpret_cast<DispatchTask*>(
                lParam));

        if (task && task->function)
            task->function();

        return 0;
    }

    return DefWindowProcW(
        window,
        message,
        wParam,
        lParam);
}

std::wstring WebView2Backend::utf8ToWide(
    const std::string& value)
{
    if (value.empty())
        return std::wstring();

    const int size =
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.c_str(),
            static_cast<int>(
                value.size()),
            0,
            0);

    if (size <= 0)
    {
        throw Error(
            "utf8_conversion",
            "Failed to convert UTF-8 to UTF-16");
    }

    std::wstring output(
        static_cast<std::size_t>(size),
        L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(
            value.size()),
        &output[0],
        size);

    return output;
}

std::string WebView2Backend::wideToUtf8(
    const wchar_t* value)
{
    if (!value || !*value)
        return std::string();

    const int length =
        static_cast<int>(
            lstrlenW(value));

    const int size =
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value,
            length,
            0,
            0,
            0,
            0);

    if (size <= 0)
    {
        throw Error(
            "utf8_conversion",
            "Failed to convert UTF-16 to UTF-8");
    }

    std::string output(
        static_cast<std::size_t>(size),
        '\0');

    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        length,
        &output[0],
        size,
        0,
        0);

    return output;
}

bool isWebView2RuntimeAvailable()
{
    LPWSTR version = 0;

    const HRESULT result =
        GetAvailableCoreWebView2BrowserVersionString(
            0,
            &version);

    const bool available =
        SUCCEEDED(result) &&
        version &&
        *version;

    if (version)
        CoTaskMemFree(version);

    return available;
}

void registerWebView2BackendFactory()
{
    if (!isWebView2RuntimeAvailable())
        return;

    registerBrowserBackendFactory(
        Engine::WebView2,
        []()
            -> std::unique_ptr<BrowserBackend>
        {
            return std::unique_ptr<BrowserBackend>(
                new WebView2Backend());
        });
}

} // namespace detail
} // namespace nativeweb
