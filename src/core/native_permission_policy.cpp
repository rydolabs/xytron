#include "core/native_permission_policy.hpp"

namespace nativeweb {
namespace detail {

NativePermissionPolicy::NativePermissionPolicy()
    : frame_(0), navigation_(0), frameActive_(false)
{
}

bool NativePermissionPolicy::validMethod(const std::string& value)
{
    if (value.empty() || value.size() > 200 ||
        value[0] == '.' || value[value.size() - 1] == '.')
        return false;

    // Exact dotted namespaces only: reject wildcards, traversal and
    // JS prototype magic identifiers. The full trust policy will also
    // validate module ownership and resolved object methods.
    if (value.find("..") != std::string::npos ||
        value.find('*') != std::string::npos ||
        value.find("__proto__") != std::string::npos ||
        value.find("prototype") != std::string::npos ||
        value.find("constructor") != std::string::npos ||
        value.find("__native_object.") == 0)
        return false;

    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '.'))
            return false;
    }
    return true;
}

void NativePermissionPolicy::adoptTrustedMainFrame(
    std::uint64_t frameId, std::uint64_t navigation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = frameId;
    navigation_ = navigation;
    frameActive_ = frameId != 0 && navigation != 0;
}

void NativePermissionPolicy::revokeFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    frameActive_ = false;
    frame_ = 0;
    navigation_ = 0;
}

bool NativePermissionPolicy::grantExactMethod(const std::string& method)
{
    if (!validMethod(method))
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return grants_.insert(method).second;
}

bool NativePermissionPolicy::revokeMethod(const std::string& method)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return grants_.erase(method) != 0;
}

bool NativePermissionPolicy::isMethodGranted(const std::string& method) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return grants_.find(method) != grants_.end();
}

void NativePermissionPolicy::revokeAllMethods()
{
    std::lock_guard<std::mutex> lock(mutex_);
    grants_.clear();
}

bool NativePermissionPolicy::allows(
    const VerifiedFrameContext& caller,
    const std::string& method) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return frameActive_ &&
           caller.engineVerified &&
           caller.mainFrame &&
           caller.trustedContent &&
           caller.frameId == frame_ &&
           caller.navigation == navigation_ &&
           grants_.find(method) != grants_.end();
}

} // namespace detail
} // namespace nativeweb
