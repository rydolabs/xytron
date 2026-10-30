#include "browser/common/backend_registry.hpp"

#include "nativeweb/error.hpp"

#include <map>
#include <mutex>

namespace nativeweb {
namespace detail {

namespace {

std::mutex& registryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<Engine, BrowserBackendFactory>& factories()
{
    static std::map<Engine, BrowserBackendFactory> value;
    return value;
}

BrowserBackendFactory findFactory(Engine engine)
{
    std::lock_guard<std::mutex> lock(registryMutex());

    std::map<Engine, BrowserBackendFactory>::const_iterator found =
        factories().find(engine);

    if (found == factories().end())
        return BrowserBackendFactory();

    return found->second;
}

Engine resolveAutoEngine()
{
#if defined(_WIN32)
    if (findFactory(Engine::WebView2))
        return Engine::WebView2;
    if (findFactory(Engine::Cef))
        return Engine::Cef;
#elif defined(__APPLE__)
    // The system WKWebView is the macOS default when its adapter is linked.
    if (findFactory(Engine::WkWebView))
        return Engine::WkWebView;
    if (findFactory(Engine::Cef))
        return Engine::Cef;
    if (findFactory(Engine::WebView2))
        return Engine::WebView2;
#else
    // Retain CEF as the Linux default; WebKitGTK is opt-in/fallback.
    if (findFactory(Engine::Cef))
        return Engine::Cef;
    if (findFactory(Engine::WebKitGtk))
        return Engine::WebKitGtk;
    if (findFactory(Engine::WebView2))
        return Engine::WebView2;
#endif

    return Engine::Auto;
}

} // namespace

void registerBrowserBackendFactory(
    Engine engine,
    const BrowserBackendFactory& factory)
{
    if (engine == Engine::Auto)
        throw Error(
            "invalid_engine",
            "Cannot register a backend factory for Engine::Auto");

    if (!factory)
        throw Error(
            "invalid_engine",
            "Browser backend factory is empty");

    std::lock_guard<std::mutex> lock(registryMutex());
    factories()[engine] = factory;
}

bool hasBrowserBackendFactory(Engine engine)
{
    if (engine == Engine::Auto)
        return resolveAutoEngine() != Engine::Auto;

    return static_cast<bool>(findFactory(engine));
}

std::unique_ptr<BrowserBackend>
createBrowserBackend(Engine requested, Engine* resolvedEngine)
{
    Engine resolved = requested;

    if (resolved == Engine::Auto)
        resolved = resolveAutoEngine();

    const BrowserBackendFactory factory =
        findFactory(resolved);

    if (!factory)
    {
        throw Error(
            "engine_unavailable",
            "Requested NativeWeb browser engine is not available");
    }

    std::unique_ptr<BrowserBackend> backend = factory();

    if (!backend)
        throw Error(
            "engine_unavailable",
            "Browser backend factory returned null");

    if (resolvedEngine)
        *resolvedEngine = resolved;

    return backend;
}

} // namespace detail
} // namespace nativeweb
