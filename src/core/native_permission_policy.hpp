#ifndef NATIVEWEB_NATIVE_PERMISSION_POLICY_HPP_INCLUDED
#define NATIVEWEB_NATIVE_PERMISSION_POLICY_HPP_INCLUDED

#include <cstdint>
#include <mutex>
#include <set>
#include <string>

namespace nativeweb {
namespace detail {

// An engine-created identity. Do not deserialize these fields from a
// JavaScript bridge payload. Backend adapters must verify origin + frame.
struct VerifiedFrameContext {
    VerifiedFrameContext()
        : frameId(0), navigation(0), engineVerified(false),
          mainFrame(false), trustedContent(false) {}
    std::uint64_t frameId;
    std::uint64_t navigation;
    bool engineVerified;
    bool mainFrame;
    bool trustedContent;
};

// Per-WebView policy foundation; NOT connected to CEF/WebView2 yet.
// A capability grant is not itself proof of a trustworthy caller.
class NativePermissionPolicy {
public:
    NativePermissionPolicy();

    // Called only by the host/backend after trusted app resource validation.
    // Invalidates previously authorized frame/navigation combinations.
    void adoptTrustedMainFrame(std::uint64_t frameId,
                               std::uint64_t navigation);
    void revokeFrame();

    // Explicit application configuration; never callable by JS request.
    bool grantExactMethod(const std::string& method);
    bool revokeMethod(const std::string& method);
    // Exact grant alone is NOT browser frame/origin authentication.
    // Call only after the engine validated the request's source.
    bool isMethodGranted(const std::string& method) const;
    void revokeAllMethods();

    bool allows(const VerifiedFrameContext& caller,
                const std::string& method) const;

private:
    static bool validMethod(const std::string& value);
    mutable std::mutex mutex_;
    std::set<std::string> grants_;
    std::uint64_t frame_;
    std::uint64_t navigation_;
    bool frameActive_;
};

} // namespace detail
} // namespace nativeweb
#endif
