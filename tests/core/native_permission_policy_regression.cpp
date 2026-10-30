#include "core/native_permission_policy.hpp"

#include <atomic>
#include <iostream>
#include <thread>

using nativeweb::detail::NativePermissionPolicy;
using nativeweb::detail::VerifiedFrameContext;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; std::cerr << \
    "security policy FAIL line " << __LINE__ << ": " << #c << '\n'; } \
    } while(0)

static VerifiedFrameContext caller(std::uint64_t frame,
                                    std::uint64_t nav)
{
    VerifiedFrameContext x;
    x.frameId = frame;
    x.navigation = nav;
    x.engineVerified = true;
    x.mainFrame = true;
    x.trustedContent = true;
    return x;
}
int main()
{
    NativePermissionPolicy policy;
    VerifiedFrameContext allowed = caller(7, 11);

    CHECK(!policy.allows(allowed, "camera.open")); // deny even trusted
    CHECK(policy.grantExactMethod("camera.open"));
    CHECK(policy.isMethodGranted("camera.open"));
    CHECK(!policy.isMethodGranted("camera.close"));
    CHECK(!policy.grantExactMethod("camera.open"));
    CHECK(!policy.grantExactMethod("camera.*"));
    CHECK(!policy.grantExactMethod("__native_object.call"));
    CHECK(!policy.grantExactMethod("camera..open"));
    CHECK(!policy.grantExactMethod("constructor.call"));
    CHECK(!policy.grantExactMethod("camera/__proto__"));
    CHECK(!policy.allows(allowed, "camera.open")); // no verified frame
    policy.adoptTrustedMainFrame(7, 11);
    CHECK(policy.allows(allowed, "camera.open"));
    CHECK(!policy.allows(allowed, "camera.close")); // exact grants

    VerifiedFrameContext forged = allowed;
    forged.engineVerified = false;
    CHECK(!policy.allows(forged, "camera.open"));
    forged = allowed;
    forged.trustedContent = false;
    CHECK(!policy.allows(forged, "camera.open"));
    forged = allowed;
    forged.mainFrame = false;
    CHECK(!policy.allows(forged, "camera.open"));
    forged = allowed;
    forged.frameId = 9;
    CHECK(!policy.allows(forged, "camera.open"));
    forged = allowed;
    forged.navigation = 10;
    CHECK(!policy.allows(forged, "camera.open"));

    policy.revokeFrame(); // navigation away / unload
    CHECK(!policy.allows(allowed, "camera.open"));
    policy.adoptTrustedMainFrame(7, 12);
    CHECK(!policy.allows(allowed, "camera.open")); // stale navigation
    VerifiedFrameContext next = caller(7, 12);
    CHECK(policy.allows(next, "camera.open"));
    CHECK(policy.revokeMethod("camera.open"));
    CHECK(!policy.isMethodGranted("camera.open"));
    CHECK(!policy.allows(next, "camera.open"));

    NativePermissionPolicy different;
    different.adoptTrustedMainFrame(7, 12);
    CHECK(!different.allows(next, "camera.open")); // per-view grants

    policy.grantExactMethod("camera.capture");
    CHECK(policy.allows(next, "camera.capture"));
    policy.revokeAllMethods();
    CHECK(!policy.allows(next, "camera.capture"));

    // Basic parallel grant/revoke vs read: concurrent callers cannot race
    // over the underlying policy containers.
    std::atomic<int> sawAllowed(0);
    std::thread reader([&]() {
        for (int i = 0; i < 1000; ++i)
            if (policy.allows(next, "camera.open")) ++sawAllowed;
    });
    std::thread writer([&]() {
        for (int i = 0; i < 1000; ++i) {
            policy.grantExactMethod("camera.open");
            policy.revokeMethod("camera.open");
        }
    });
    reader.join();
    writer.join();
    CHECK(!policy.allows(next, "camera.open"));
    if (failures) return 1;
    std::cout << "Native permission policy foundation: PASS" << std::endl;
    return 0;
}
