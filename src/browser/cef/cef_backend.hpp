#ifndef NATIVEWEB_CEF_BACKEND_HPP_INCLUDED
#define NATIVEWEB_CEF_BACKEND_HPP_INCLUDED

#include "browser/common/browser_backend.hpp"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_request_context.h"

namespace nativeweb {
namespace detail {

class CefBackend : public BrowserBackend
{
public:
    CefBackend();
    ~CefBackend() override;

    const char* engineName() const override;
    Engine engine() const override;
    Capabilities capabilities() const override;

    void setListener(BrowserBackendListener* listener) override;

    void create(const BrowserCreateParams& params) override;
    void destroy() override;
    bool isCreated() const override;

    void load(const std::string& source) override;
    void reload() override;

    void postBridgeMessage(const Any& message) override;

private:
    class Client;
    friend class Client;

    void onAfterCreated(CefRefPtr<CefBrowser> browser);
    void onBeforeClose(CefRefPtr<CefBrowser> browser);
    void onLoadStart(CefRefPtr<CefFrame> frame);
    void onLoadEnd(CefRefPtr<CefFrame> frame);
    bool onProcessMessage(
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefProcessMessage> message);

    BrowserBackendListener* listener_;
    CefRefPtr<Client> client_;
    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<CefRequestContext> appRequestContext_;
    std::string trustedEntryUrl_;
    std::uint64_t navigation_ = 0;
    bool mainFrameReady_ = false;
    std::string activeMainFrameIdentifier_;
    std::string documentSessionToken_;
};

void registerCefBackendFactory();

} // namespace detail
} // namespace nativeweb

#endif
