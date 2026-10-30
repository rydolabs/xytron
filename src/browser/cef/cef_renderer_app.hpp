#ifndef NATIVEWEB_CEF_RENDERER_APP_HPP_INCLUDED
#define NATIVEWEB_CEF_RENDERER_APP_HPP_INCLUDED

#include "include/cef_app.h"
#include "include/cef_v8.h"

#include <cstdint>
#include <functional>
#include <map>
#include <vector>
#include <string>

namespace nativeweb {
namespace detail {

class CefRendererApp :
    public CefApp,
    public CefBrowserProcessHandler,
    public CefRenderProcessHandler
{
public:
    CefRendererApp();

    CefRefPtr<CefBrowserProcessHandler>
    GetBrowserProcessHandler() override
    {
        return this;
    }

    CefRefPtr<CefRenderProcessHandler>
    GetRenderProcessHandler() override
    {
        return this;
    }

    void setBrowserReadyCallback(
        const std::function<void()>& callback);

    void OnRegisterCustomSchemes(
        CefRawPtr<CefSchemeRegistrar> registrar) override;

    void OnContextInitialized() override;

    void OnContextCreated(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override;

    void OnContextReleased(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override;

    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefProcessId sourceProcess,
        CefRefPtr<CefProcessMessage> message) override;

    std::string beginInvoke(
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context,
        CefRefPtr<CefV8Value> promise,
        const CefString& method,
        const CefV8ValueList& arguments);

    bool cancelInvoke(const std::string& requestId,
                      CefRefPtr<CefV8Context> caller,
                      const std::string& reason);

    std::string subscribeEvent(
        CefRefPtr<CefV8Context> context,
        const CefString& eventName,
        CefRefPtr<CefV8Value> callback);

    bool unsubscribeEvent(const std::string& subscriptionId);

private:
    struct PendingPromise
    {
        CefRefPtr<CefV8Context> context;
        CefRefPtr<CefV8Value> promise;
    };

    struct DocumentSession
    {
        CefRefPtr<CefV8Context> context;
        std::string token;
    };

    struct QueuedDocumentRequest
    {
        CefRefPtr<CefFrame> frame;
        CefRefPtr<CefV8Context> context;
        CefRefPtr<CefProcessMessage> message;
    };

    struct EventSubscription
    {
        CefRefPtr<CefV8Context> context;
        CefRefPtr<CefV8Value> callback;
        std::string eventName;
        int browserId;
    };

    class InvokeHandler;

    std::function<void()> browserReadyCallback_;
    std::uint64_t nextRequestId_;
    std::uint64_t nextSubscriptionId_;
    std::map<std::string, PendingPromise> pending_;
    std::vector<DocumentSession> documentSessions_;
    std::vector<QueuedDocumentRequest> queuedDocumentRequests_;
    std::map<std::string, EventSubscription> subscriptions_;

    IMPLEMENT_REFCOUNTING(CefRendererApp);
};

} // namespace detail
} // namespace nativeweb

#endif
