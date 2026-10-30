#include "nativeweb/nativeweb.hpp"

#include <future>
#include <memory>
#include <string>

namespace {

class CompileListener : public nativeweb::WebViewListener
{
public:
    void onCreated() override
    {
    }

    void onClosed() override
    {
    }
};

struct CompileDevice
{
    int value;
};

struct CompileRootService
{
    explicit CompileRootService(int value)
        : base(value)
    {
    }

    int add(int value)
    {
        return base + value;
    }

    int sub(int value) const
    {
        return base - value;
    }

    int base;
};

Any dynamicAdd(const VariantList& args)
{
    const int a = AnyCast<int>(args[0]);
    const int b = AnyCast<int>(args[1]);
    return Any(a + b);
}

void compileBeginnerContract(nativeweb::NativeWindowHandle parent)
{
    nativeweb::WebView webview;
    CompileListener listener;
    webview.setListener(&listener);

    nativeweb::WebViewOptions options;
    options.engine = nativeweb::Engine::Auto;

    webview.create(parent, "index.html", options);

    const nativeweb::Engine engine = webview.engine();
    const nativeweb::Capabilities capabilities =
        webview.capabilities();
    const bool hasBinary =
        capabilities.supports(nativeweb::Capability::Binary);
    webview.load("index.html");

    webview.bind("math.dynamicAdd", &dynamicAdd);

    webview.bind("math.add", [](int a, int b) {
        return a + b;
    });

    webview.bind(
        "rootRaw",
        new CompileRootService(40))
        .method("add", &CompileRootService::add)
        .method("sub", &CompileRootService::sub);

    std::shared_ptr<CompileRootService> rootShared(
        new CompileRootService(50));

    webview.bind(
        "rootShared",
        rootShared)
        .method("add", &CompileRootService::add);

    VariantList args;
    args.push_back(Any(3));
    args.push_back(Any(4));

    std::future<Any> dynamicResult =
        webview.execute("ui.calculate", args);

    std::future<int> typedResult =
        webview.execute<int>("ui.calculate", 3, 4);

    std::shared_ptr<CompileDevice> device(
        new CompileDevice());

    device->value = 40;

    const nativeweb::NativeObjectHandle handle =
        webview.addObject(
            device,
            "CompileDevice");

    webview.bindObjectMethod(
        handle,
        "add",
        [device](int value) {
            return device->value + value;
        });

    const bool released =
        webview.releaseObject(handle);

    webview.emit("app.ready", Any(true));
    webview.reload();
    webview.destroy();

    (void)dynamicResult;
    (void)engine;
    (void)hasBinary;
    (void)typedResult;
    (void)released;
}

} // namespace
