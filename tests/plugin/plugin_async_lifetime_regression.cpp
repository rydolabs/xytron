#include "browser/common/backend_registry.hpp"
#include "core/bridge_message.hpp"
#include "nativeweb/webview.hpp"
#include "plugin/shared_library.hpp"

#include "test_require.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace {

class FakeBackend : public nativeweb::detail::BrowserBackend
{
public:
    FakeBackend() : listener_(0), created_(false) { current = this; }
    ~FakeBackend() override { if (current == this) current = 0; }

    const char* engineName() const override { return "fake-cef"; }
    nativeweb::Engine engine() const override { return nativeweb::Engine::Cef; }
    nativeweb::Capabilities capabilities() const override { return nativeweb::Capabilities(); }
    void setListener(nativeweb::detail::BrowserBackendListener* listener) override {
        listener_ = listener;
    }
    void create(const nativeweb::detail::BrowserCreateParams&) override {
        created_ = true;
        if (listener_) listener_->onBrowserCreated();
    }
    void destroy() override {
        created_ = false;
        if (listener_) listener_->onBrowserClosed();
    }
    bool isCreated() const override { return created_; }
    void load(const std::string&) override {}
    void reload() override {}
    void postBridgeMessage(const Any&) override {}
    void deliver(const Any& request) {
        NW_REQUIRE(listener_);
        listener_->onBridgeMessage(request);
    }
    static FakeBackend* current;
private:
    nativeweb::detail::BrowserBackendListener* listener_;
    bool created_;
};
FakeBackend* FakeBackend::current = 0;

bool waitUntil(const std::function<bool()>& ready)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ready();
}

void createWebView(std::unique_ptr<nativeweb::WebView>& web)
{
    web.reset(new nativeweb::WebView());
    nativeweb::WebViewOptions options;
    options.engine = nativeweb::Engine::Cef;
    web->create(0, "test.html", options);
    NW_REQUIRE(FakeBackend::current != 0);
}

void testInFlightObjectCall(const char* path,
    int32_t (*running)(), int32_t (*completed)())
{
    std::unique_ptr<nativeweb::WebView> web;
    createWebView(web);
    const std::string id = web->loadPlugin(path);
    const nativeweb::NativeObjectHandle handle =
        web->createPluginObject(id, "counter");
    const int finishedBefore = completed();
    VariantList args;
    args.push_back(Any(std::to_string(handle.id)));
    args.push_back(Any(handle.type));
    args.push_back(Any(std::string("slow_add")));
    args.push_back(Any(3));
    FakeBackend::current->deliver(nativeweb::detail::makeRequestMessage(
        5001, "__native_object.call", args));
    NW_REQUIRE(waitUntil([&]() { return running() > 0; }));

    // WorkerPool owns a snapshot of the callback; WebView may disappear
    // while that callback still executes plugin code.
    web.reset();
    NW_REQUIRE(waitUntil([&]() { return completed() == finishedBefore + 1; }));
}

void testInFlightFactory(const char* path,
    int32_t (*running)(), int32_t (*completed)())
{
    std::unique_ptr<nativeweb::WebView> web;
    createWebView(web);
    const std::string id = web->loadPlugin(path);
    web->bindPluginFactory("camera.openSlow", id, "slow_counter");
    const int finishedBefore = completed();
    FakeBackend::current->deliver(nativeweb::detail::makeRequestMessage(
        5002, "camera.openSlow", VariantList()));
    NW_REQUIRE(waitUntil([&]() { return running() > 0; }));

    // stop() must wait for construction before clearing native objects,
    // and queued functions must retain only independently-owned state.
    web.reset();
    NW_REQUIRE(completed() == finishedBefore + 1);
    NW_REQUIRE(running() == 0);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    nativeweb::detail::SharedLibrary probe(argv[1]);
    typedef int32_t (*Counter)();
    const Counter runningCalls = reinterpret_cast<Counter>(
        probe.symbol("nativeweb_test_running_calls"));
    const Counter completedCalls = reinterpret_cast<Counter>(
        probe.symbol("nativeweb_test_completed_calls"));
    const Counter runningFactories = reinterpret_cast<Counter>(
        probe.symbol("nativeweb_test_running_factories"));
    const Counter completedFactories = reinterpret_cast<Counter>(
        probe.symbol("nativeweb_test_completed_factories"));

    nativeweb::detail::registerBrowserBackendFactory(nativeweb::Engine::Cef,
        []() -> std::unique_ptr<nativeweb::detail::BrowserBackend> {
            return std::unique_ptr<nativeweb::detail::BrowserBackend>(new FakeBackend());
        });
    testInFlightObjectCall(argv[1], runningCalls, completedCalls);
    testInFlightFactory(argv[1], runningFactories, completedFactories);
    return 0;
}
