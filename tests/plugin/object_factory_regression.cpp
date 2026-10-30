#include "plugin/plugin_module.hpp"
#include "core/native_object_runtime.hpp"
#include "nativeweb/error.hpp"

#include "test_require.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

namespace {

using nativeweb::detail::BindingRegistry;
using nativeweb::detail::NativeObjectRuntime;
using nativeweb::detail::PluginModule;

VariantList integerArg(int number)
{
    VariantList args;
    args.push_back(Any(number));
    return args;
}

void expectError(const std::string& expected, const std::function<void()>& op)
{
    bool observed = false;
    try { op(); }
    catch (const nativeweb::Error& e) {
        observed = true;
        NW_REQUIRE(e.code() == expected);
    }
    NW_REQUIRE(observed);
}

void testMethodsAndModuleDestructor(const char* path)
{
    BindingRegistry registry;
    NativeObjectRuntime runtime;
    nativeweb::NativeObjectHandle handle;
    {
        PluginModule module(path, registry);
        NW_REQUIRE(module.objectFactoryCount() == 3);
        handle = module.createObject("counter", runtime);
        NW_REQUIRE(handle.valid());
        NW_REQUIRE(handle.type == "objects.counter");
        NW_REQUIRE(AnyCast<int>(runtime.call(handle, "value", VariantList())) == 42);
        NW_REQUIRE(AnyCast<int>(runtime.call(handle, "add", integerArg(8))) == 50);
        NW_REQUIRE(AnyCast<int>(runtime.call(handle, "value", VariantList())) == 50);
        expectError("counter_failure", [&]() {
            runtime.call(handle, "fail", VariantList());
        });
        expectError("native_object_method_not_found", [&]() {
            runtime.call(handle, "missing", VariantList());
        });
        expectError("plugin_object_factory_not_found", [&]() {
            module.createObject("unknown", runtime);
        });
    }
    // Module destructor removes registrations but keeps the SO/DLL loaded
    // as long as the native object still needs plugin methods/destruction.
    NW_REQUIRE(AnyCast<int>(runtime.call(handle, "add", integerArg(1))) == 51);
    NW_REQUIRE(runtime.release(handle));
    NW_REQUIRE(!runtime.release(handle));
    expectError("native_object_not_found", [&]() {
        runtime.call(handle, "value", VariantList());
    });
}

void testExplicitUnloadLiveObject(const char* path)
{
    BindingRegistry registry;
    NativeObjectRuntime runtime;
    PluginModule module(path, registry);
    const nativeweb::NativeObjectHandle handle =
        module.createObject("counter", runtime);
    expectError("plugin_busy", [&]() { module.unload(); });
    NW_REQUIRE(!module.unloaded());
    expectError("plugin_unloading", [&]() {
        module.createObject("counter", runtime);
    });
    NW_REQUIRE(AnyCast<int>(runtime.call(handle, "value", VariantList())) == 42);
    NW_REQUIRE(runtime.release(handle));
    module.unload();
    NW_REQUIRE(module.unloaded());
    module.unload(); // idempotent
    NW_REQUIRE(module.path() == path);
}

void testImmediateUnload(const char* path)
{
    BindingRegistry registry;
    PluginModule module(path, registry);
    NW_REQUIRE(module.registrationCount() == 0);
    NW_REQUIRE(module.objectFactoryCount() == 3);
    module.unload();
    NW_REQUIRE(module.unloaded());
    NW_REQUIRE(module.objectFactoryCount() == 0);
    expectError("plugin_unloading", [&]() {
        NativeObjectRuntime runtime;
        module.createObject("counter", runtime);
    });
}

void testInFlightRelease(const char* path)
{
    BindingRegistry registry;
    NativeObjectRuntime runtime;
    PluginModule module(path, registry);
    const nativeweb::NativeObjectHandle handle =
        module.createObject("counter", runtime);

    std::future<int> result = std::async(std::launch::async, [&]() {
        return AnyCast<int>(runtime.call(handle, "slow_add", integerArg(10)));
    });
    bool active = false;
    const std::chrono::steady_clock::time_point until =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until) {
        if (AnyCast<bool>(runtime.call(handle, "running", VariantList()))) {
            active = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NW_REQUIRE(active);
    // Registry entry disappears but the in-flight method holds a live object.
    NW_REQUIRE(runtime.release(handle));
    expectError("plugin_busy", [&]() { module.unload(); });
    NW_REQUIRE(result.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    NW_REQUIRE(result.get() == 52);
    module.unload();
    NW_REQUIRE(module.unloaded());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    testMethodsAndModuleDestructor(argv[1]);
    testExplicitUnloadLiveObject(argv[1]);
    testImmediateUnload(argv[1]);
    testInFlightRelease(argv[1]);
    std::cout << "Plugin object methods/unload lifetime: PASS\n";
    return 0;
}
