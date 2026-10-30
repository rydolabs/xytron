#ifndef NATIVEWEB_BROWSER_BACKEND_HPP_INCLUDED
#define NATIVEWEB_BROWSER_BACKEND_HPP_INCLUDED

#include "nativeweb/any.hpp"
#include "nativeweb/types.hpp"
#include "core/native_permission_policy.hpp"

#include <string>
#include <memory>

namespace nativeweb {
namespace detail {

struct BrowserCreateParams
{
    BrowserCreateParams()
        : parent(0)
    {
    }

    NativeWindowHandle parent;
    std::string source;
    std::shared_ptr<const AppResourceBundle> appResources;
};

class BrowserBackendListener
{
public:
    virtual ~BrowserBackendListener()
    {
    }

    virtual void onBrowserCreated() = 0;
    virtual void onLoadStarted(const std::string& source) = 0;
    virtual void onLoadFinished(const std::string& source) = 0;

    // Browser engines deliver structured bridge envelopes through this method.
    // The Core/Bridge layer owns request IDs, method routing and Promise logic.
    virtual void onBridgeMessage(const Any& message) = 0;

    // Engine-derived identity; NEVER deserialize from a renderer request.
    virtual void onTrustedFrameActivated(const VerifiedFrameContext&) {}
    virtual void onVerifiedBridgeMessage(const Any& message,
                                        const VerifiedFrameContext&)
    {
        onBridgeMessage(message);
    }

    virtual void onBrowserClosed() = 0;
};

class BrowserBackend
{
public:
    virtual ~BrowserBackend()
    {
    }

    virtual const char* engineName() const = 0;
    virtual Engine engine() const = 0;
    virtual Capabilities capabilities() const = 0;

    virtual void setListener(BrowserBackendListener* listener) = 0;

    virtual void create(const BrowserCreateParams& params) = 0;
    virtual void destroy() = 0;
    virtual bool isCreated() const = 0;

    virtual void load(const std::string& source) = 0;
    virtual void reload() = 0;

    // Core -> renderer structured message transport. This method may be called
    // from a NativeWeb worker thread; each backend must marshal to its required
    // browser/engine thread internally. No raw-JavaScript string concatenation
    // is part of this contract.
    virtual void postBridgeMessage(const Any& message) = 0;
};

} // namespace detail
} // namespace nativeweb

#endif
