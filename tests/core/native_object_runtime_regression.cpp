#include "core/native_object_runtime.hpp"
#include "nativeweb/error.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

int failures = 0;

void fail(
    const char* expression,
    const char* file,
    int line)
{
    std::cerr << file << ":" << line
              << ": CHECK failed: "
              << expression << std::endl;
    ++failures;
}

#define CHECK(expr) \
    do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

struct Camera
{
    explicit Camera(int value)
        : base(value)
    {
    }

    int base;
};

struct OwnerProbe
{
    explicit OwnerProbe(int* value)
        : destroyed(value)
    {
    }

    ~OwnerProbe()
    {
        ++(*destroyed);
    }

    int* destroyed;
};

void testObjectLifecycleAndMethods()
{
    nativeweb::detail::NativeObjectRuntime runtime;

    std::shared_ptr<Camera> camera(
        new Camera(40));

    const nativeweb::NativeObjectHandle handle =
        runtime.add(
            std::static_pointer_cast<void>(camera),
            "Camera");

    CHECK(handle.valid());
    CHECK(handle.type == "Camera");
    CHECK(runtime.size() == 1u);

    runtime.bindMethod(
        handle,
        "add",
        nativeweb::detail::makeDynamicFunction(
            [camera](int value) {
                return camera->base + value;
            }));

    VariantList args;
    args.push_back(Any(2));

    CHECK(
        AnyCast<int>(
            runtime.call(
                handle,
                "add",
                args)) == 42);

    CHECK(runtime.release(handle));
    CHECK(runtime.size() == 0u);
    CHECK(!runtime.release(handle));

    bool missing = false;

    try
    {
        (void)runtime.call(
            handle,
            "add",
            args);
    }
    catch (const nativeweb::Error& error)
    {
        missing =
            error.code() ==
            "native_object_not_found";
    }

    CHECK(missing);
}

void testObjectRetainsOwnerLease()
{
    int ownerDestroyed = 0;

    nativeweb::detail::NativeObjectRuntime runtime;

    std::shared_ptr<Camera> camera(
        new Camera(7));

    std::shared_ptr<OwnerProbe> owner(
        new OwnerProbe(
            &ownerDestroyed));

    const nativeweb::NativeObjectHandle handle =
        runtime.add(
            std::static_pointer_cast<void>(
                camera),
            "Camera",
            std::static_pointer_cast<void>(
                owner));

    owner.reset();

    CHECK(ownerDestroyed == 0);
    CHECK(runtime.size() == 1u);

    CHECK(runtime.release(handle));
    CHECK(ownerDestroyed == 1);
}

} // namespace

int main()
{
    testObjectLifecycleAndMethods();
    testObjectRetainsOwnerLease();

    if (failures != 0)
    {
        std::cerr
            << "Native object runtime regression failed: "
            << failures
            << " check(s)"
            << std::endl;
        return 1;
    }

    std::cout
        << "Native object runtime regression: PASS"
        << std::endl;

    return 0;
}
