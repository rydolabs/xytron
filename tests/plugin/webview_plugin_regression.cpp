#include "nativeweb/webview.hpp"
#include "nativeweb/error.hpp"
#include "plugin/shared_library.hpp"
#include "test_require.hpp"
#include <functional>
#include <future>
#include <thread>
#include <atomic>
#include <string>

static void expect(const char* code, const std::function<void()>& action)
{
    bool caught = false;
    try { action(); }
    catch (const nativeweb::Error& e) {
        caught = true;
        NW_REQUIRE(e.code() == code);
    }
    NW_REQUIRE(caught);
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    nativeweb::detail::SharedLibrary probe(argv[1]);
    typedef int32_t (*CountFunction)();
    const CountFunction registrationCalls = reinterpret_cast<CountFunction>(
        probe.symbol("nativeweb_test_registration_calls"));
    nativeweb::WebView web;
    const std::string id = web.loadPlugin(argv[1]);
    const int firstRegistrationCount = registrationCalls();
    NW_REQUIRE(firstRegistrationCount == 1);
    // This must reject before register_api; the first plugin's bindings
    // must not be replaced by registrations from the duplicate.
    expect("plugin_duplicate", [&]() { web.loadPlugin(argv[1]); });
    NW_REQUIRE(registrationCalls() == firstRegistrationCount);

    NW_REQUIRE(id == "objects");
    web.bindPluginFactory("camera.open", id, "counter");
    expect("plugin_not_found", [&]() { web.createPluginObject("missing", "counter"); });
    nativeweb::VariantList args;
    args.push_back(nativeweb::Any(77));
    const nativeweb::NativeObjectHandle parameterized =
        web.createPluginObject(id, "counter_args", args);
    NW_REQUIRE(parameterized.valid());
    expect("invalid_factory_arguments", [&]() {
        web.createPluginObject(id, "counter_args");
    });
    expect("plugin_factory_arguments_unsupported", [&]() {
        web.createPluginObject(id, "counter", args);
    });
    NW_REQUIRE(web.releaseObject(parameterized));
    const nativeweb::NativeObjectHandle object = web.createPluginObject(id, "counter");
    NW_REQUIRE(object.valid());
    NW_REQUIRE(object.type == "objects.counter");
    expect("plugin_busy", [&]() { web.unloadPlugin(id); });
    expect("plugin_unloading", [&]() { web.createPluginObject(id, "counter"); });
    NW_REQUIRE(web.releaseObject(object));
    web.unloadPlugin(id);
    expect("plugin_not_found", [&]() { web.unloadPlugin(id); });

    // The plugin manager must also serialize concurrent duplicate attempts.
    std::atomic<int> successful(0);
    std::atomic<int> duplicates(0);
    std::vector<std::future<void> > tasks;
    for (int i = 0; i < 4; ++i) {
        tasks.push_back(std::async(std::launch::async, [&]() {
            try {
                web.loadPlugin(argv[1]);
                ++successful;
            } catch (const nativeweb::Error& error) {
                if (error.code() == "plugin_duplicate") ++duplicates;
                else throw;
            }
        }));
    }
    for (std::size_t i = 0; i < tasks.size(); ++i) tasks[i].get();
    NW_REQUIRE(successful == 1);
    NW_REQUIRE(duplicates == 3);
    NW_REQUIRE(registrationCalls() == firstRegistrationCount + 1);
    web.unloadPlugin(id);
    return 0;
}
