#ifndef NATIVEWEB_BACKEND_REGISTRY_HPP_INCLUDED
#define NATIVEWEB_BACKEND_REGISTRY_HPP_INCLUDED

#include "browser/common/browser_backend.hpp"

#include <functional>
#include <memory>

namespace nativeweb {
namespace detail {

typedef std::function<std::unique_ptr<BrowserBackend>()>
    BrowserBackendFactory;

void registerBrowserBackendFactory(
    Engine engine,
    const BrowserBackendFactory& factory);

bool hasBrowserBackendFactory(Engine engine);

std::unique_ptr<BrowserBackend>
createBrowserBackend(Engine requested, Engine* resolvedEngine);

} // namespace detail
} // namespace nativeweb

#endif
