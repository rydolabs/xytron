#ifndef NATIVEWEB_WKWEBVIEW_BACKEND_HPP_INCLUDED
#define NATIVEWEB_WKWEBVIEW_BACKEND_HPP_INCLUDED

#ifndef __APPLE__
#error WKWebView is available only on Apple platforms
#endif

#include "browser/common/browser_backend.hpp"
#include <atomic>
#include <memory>

namespace nativeweb { namespace detail {

// Cocoa/AppKit and WebKit ownership remain inside the Objective-C++ impl.
// Construction, navigation and disposal run on the macOS main thread.
class WkWebViewBackend final : public BrowserBackend
{
public:
    WkWebViewBackend();
    ~WkWebViewBackend() override;

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

    // Invoked by WebKit delegates on the main thread. Not an external API.
    void navigationStarted(const std::string& url);
    void navigationFinished(const std::string& url);
    void scriptMessage(const std::string& json, const std::string& callerUrl,
                       bool mainFrame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BrowserBackendListener* listener_;
    std::string trustedEntry_;
    std::string session_;
    std::uint64_t navigation_;
    bool ready_;
    // Stable ownership across worker callbacks; only Cocoa's main thread
    // may touch Impl / WebKit instances. Invalidate scheduled sends on reload.
    std::shared_ptr<std::atomic<bool> > dispatchActive_;
    std::atomic<std::uint64_t> documentEpoch_;
};

void registerWkWebViewBackendFactory();

}} // namespace nativeweb::detail
#endif
