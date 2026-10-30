#include "browser/cef/cef_backend.hpp"
#include "browser/cef/cef_renderer_app.hpp"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "nativeweb/error.hpp"
#include "nativeweb/webview.hpp"
#include "nativeweb/app_resources.hpp"
#include <fstream>
#include <sstream>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

struct Apple
{
    explicit Apple(int value)
        : base(value)
    {
    }

    int add(int a, int b)
    {
        return base + a + b;
    }

    int sub(int a, int b) const
    {
        return base + a - b;
    }

    std::string name() const
    {
        return "apple";
    }

    int base;
};

struct Camera
{
    explicit Camera(int value)
        : base(value)
    {
    }

    int base;
};

class IntegrationListener : public nativeweb::WebViewListener
{
public:
    explicit IntegrationListener(nativeweb::WebView& webview)
        : webview_(webview),
          success_(false),
          cppCallsStarted_(false),
          cppCallsEvaluated_(false),
          cppCallsSuccess_(false),
          pendingDestroyStarted_(false),
          browserThreadKnown_(false),
          activeConcurrentCalls_(0),
          maxConcurrentCalls_(0)
    {
        webview_.bind(
            "math.add",
            [](int a, int b) {
                return a + b;
            });

        webview_.bind(
            "apple",
            new Apple(10))
            .method(
                "add",
                &Apple::add)
            .method(
                "sub",
                &Apple::sub)
            .method(
                "name",
                &Apple::name);

        webview_.bind(
            "echo.object",
            [](const VariantDict& value) {
                return value;
            });

        webview_.bind(
            "echo.array",
            [](const VariantList& value) {
                return value;
            });

        webview_.bind(
            "test.emit",
            [this](const VariantDict& payload) {
                webview_.emit(
                    "test.event",
                    Any(payload));
                return true;
            });

        const std::shared_ptr<std::atomic<int> > messageCount(new std::atomic<int>(0));
        webview_.bind("test.message.count", [messageCount]() {
            return messageCount->load();
        });
        webview_.onMessage("test.message.ping", [this,messageCount](const Any& data) {
            messageCount->fetch_add(1);
            bool valid = false;
            if (data.type() == typeid(VariantDict)) {
                const VariantDict fields = AnyCast<VariantDict>(data);
                const VariantDict::const_iterator value = fields.find("value");
                valid = value != fields.end() && value->second.type() == typeid(int) &&
                        AnyCast<int>(value->second) == 42;
            }
            nativeweb::Binary response;
            if (valid) {
                response.push_back(0);
                response.push_back(127);
                response.push_back(255);
            }
            webview_.postMessage("test.message.pong", Any(response));
        });
        webview_.onMessage("test.message.binary", [this](const Any& data) {
            const bool valid = data.type() == typeid(nativeweb::Binary) &&
                AnyCast<const nativeweb::Binary&>(data).size() == 3 &&
                AnyCast<const nativeweb::Binary&>(data)[0] == 0 &&
                AnyCast<const nativeweb::Binary&>(data)[1] == 127 &&
                AnyCast<const nativeweb::Binary&>(data)[2] == 255;
            VariantDict result;
            result["valid"] = valid;
            webview_.postMessage("test.message.binary.ok", Any(result));
        });

        webview_.bind("test.binaryEcho", [](const nativeweb::Binary& bytes) {
            return bytes;
        });
        webview_.bind("test.binaryNested", [](const VariantDict& value) {
            return value;
        });

        webview_.bind(
            "test.binary",
            []() {
                nativeweb::Binary bytes;
                bytes.push_back(0);
                bytes.push_back(127);
                bytes.push_back(255);
                return bytes;
            });

        webview_.bind(
            "camera.open",
            [this]() {
                std::shared_ptr<Camera> camera(
                    new Camera(40));

                const nativeweb::NativeObjectHandle handle =
                    webview_.addObject(
                        camera,
                        "Camera");

                webview_.bindObjectMethod(
                    handle,
                    "add",
                    [camera](int value) {
                        return camera->base + value;
                    });

                webview_.bindObjectMethod(
                    handle,
                    "name",
                    [camera]() {
                        return std::string("camera");
                    });

                return handle;
            });

        webview_.bind(
            "camera.accept",
            [](const nativeweb::NativeObjectHandle& handle) {
                return handle.valid() &&
                    handle.type == "Camera";
            });

        // Isolated benchmark fixture: results printed as a single JSON
        // record; only this opt-in page calls bench.finish/abort.
        webview_.bind("bench.echo", [](const nativeweb::Binary& bytes) {
            return bytes;
        });
        webview_.onMessage("bench.ping", [this](const Any& payload) {
            webview_.postMessage("bench.pong", payload);
        });
        webview_.bind("bench.config", []() {
            VariantDict out;
            const char* w=std::getenv("XYTRON_BENCH_WARMUP");
            const char* s=std::getenv("XYTRON_BENCH_SAMPLES");
            out["warmup"]=w ? std::atoi(w) : 20;
            out["samples"]=s ? std::atoi(s) : 100;
            return out;
        });
        webview_.bind("bench.finish", [this](const std::string& json) {
            if (json.size()>1000000u || json.empty()) {
                failure_="Invalid benchmark result payload";
                success_=false;
            } else {
                std::cout << "xytron-benchmark-json:" << json << std::endl;
                success_=true;
            }
            webview_.destroy();
            return true;
        });
        webview_.bind("bench.abort", [this](const std::string& message) {
            failure_="Benchmark failed: " + message;
            success_=false;
            std::cerr << "bridge-fail: " << failure_ << std::endl;
            webview_.destroy();
            return false;
        });

        webview_.bind("test.cancel.delay", [](int durationMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
            return durationMs;
        });

        // Verify that retired-session requests never reach native dispatch.
        const std::shared_ptr<std::atomic<int> > replayEffects(
            new std::atomic<int>(0));
        webview_.bind("test.replay.bump", [replayEffects]() {
            return replayEffects->fetch_add(1) + 1;
        });
        webview_.bind("test.replay.count", [replayEffects]() {
            return replayEffects->load();
        });

        webview_.bind(
            "test.isWorkerThread",
            [this]() {
                return
                    browserThreadKnown_.load() &&
                    std::this_thread::get_id() !=
                        browserThreadId_;
            });

        webview_.bind(
            "test.concurrentProbe",
            [this](int value) {
                const int active =
                    activeConcurrentCalls_.fetch_add(1) + 1;

                int observed =
                    maxConcurrentCalls_.load();

                while (
                    active > observed &&
                    !maxConcurrentCalls_.compare_exchange_weak(
                        observed,
                        active))
                {
                }

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(80));

                activeConcurrentCalls_.fetch_sub(1);
                return value;
            });

        webview_.bind(
            "test.maxConcurrency",
            [this]() {
                return maxConcurrentCalls_.load();
            });

        webview_.bind(
            "test.rejectExpected",
            []() -> int {
                throw nativeweb::Error(
                    "expected_error",
                    "expected structured rejection");
            });

        webview_.bind(
            "test.checkpoint",
            [](const std::string& name) {
                std::cout
                    << "checkpoint: "
                    << name
                    << std::endl;
                return true;
            });

        webview_.bind(
            "test.throw",
            []() -> int {
                throw std::runtime_error(
                    "expected native exception");
            });

        webview_.bind(
            "test.beginCppCalls",
            [this]() {
                beginCppCalls();
                return true;
            });

        webview_.bind(
            "test.cppCallsStatus",
            [this]() {
                return cppCallsStatus();
            });

        webview_.bind(
            "test.beginPendingDestroy",
            [this]() {
                if (!pendingDestroyStarted_)
                {
                    pendingDestroyResult_ =
                        webview_.execute<int>(
                            "ui.neverResolve");

                    pendingDestroyStarted_ = true;
                }

                return true;
            });

        webview_.bind(
            "test.destroyWithPending",
            [this]() {
                if (!pendingDestroyStarted_)
                {
                    failure_ =
                        "Pending-destroy call was not started";
                    success_ = false;
                    webview_.destroy();
                    return false;
                }

                webview_.destroy();

                const std::future_status status =
                    pendingDestroyResult_.wait_for(
                        std::chrono::seconds(2));

                bool rejected = false;

                if (status == std::future_status::ready)
                {
                    try
                    {
                        (void)pendingDestroyResult_.get();

                        if (failure_.empty())
                        {
                            failure_ =
                                "Pending JS call resolved "
                                "during WebView destroy";
                        }
                    }
                    catch (const nativeweb::Error& error)
                    {
                        rejected =
                            error.code() ==
                            "webview_destroyed";

                        if (!rejected &&
                            failure_.empty())
                        {
                            failure_ =
                                std::string(
                                    "Unexpected pending-call "
                                    "destroy error: ") +
                                error.code() +
                                ": " +
                                error.what();
                        }
                    }
                    catch (const std::exception& error)
                    {
                        if (failure_.empty())
                        {
                            failure_ =
                                std::string(
                                    "Unexpected pending-call "
                                    "destroy exception: ") +
                                error.what();
                        }
                    }
                }
                else if (failure_.empty())
                {
                    failure_ =
                        "Pending C++->JS future did not "
                        "finish after WebView destroy";
                }

                success_ =
                    success_ &&
                    rejected &&
                    failure_.empty();

                return rejected;
            });

        webview_.bind(
            "test.done",
            [this](int result) {
                const VariantDict status =
                    cppCallsStatus();

                const bool cppDone =
                    AnyCast<bool>(
                        status.find("done")->second);

                const bool cppSuccess =
                    AnyCast<bool>(
                        status.find("success")->second);

                success_ =
                    result == 7 &&
                    cppDone &&
                    cppSuccess;

                if (!success_ && failure_.empty())
                {
                    failure_ =
                        "Public WebView regression returned "
                        "an unexpected result";
                }

                return success_;
            });

        webview_.bind(
            "test.fail",
            [this](const std::string& message) {
                std::cerr
                    << "bridge-fail: "
                    << message
                    << std::endl;
                failure_ = message;
                success_ = false;
                webview_.destroy();
                return false;
            });
    }

    void configureManaged(const std::string& python,const std::string& fixture,
                          const std::string& dotnetRuntime, const std::string& dotnetAssembly)
    {
        if ((python.empty() || fixture.empty()) &&
            (dotnetRuntime.empty() || dotnetAssembly.empty())) return;
        if (!python.empty() && !dotnetAssembly.empty())
            throw nativeweb::Error("invalid_test_mode", "Only one managed runtime per browser");
        managedPython_=python;
        managedFixture_=fixture;
        managedDotnetRuntime_=dotnetRuntime;
        managedDotnetAssembly_=dotnetAssembly;
        webview_.bind("test.managed.ready",[this](){return managedReady_.load();});
        webview_.bind("test.managed.done",[this](){
            success_=true;
            std::cout << (managedDotnetAssembly_.empty()
                ? "checkpoint: managed-python-complete"
                : "checkpoint: managed-dotnet-complete") << std::endl;
            webview_.destroy();
            return true;
        });
    }

    // Required only in --plugin-path mode. The browser page drives this
    // test through real JavaScript Promise calls, not a fake backend.
    void configurePlugin(const std::string& path)
    {
        if (path.empty())
            return;

        const std::string id = webview_.loadPlugin(path);
        if (id != "objects")
            throw nativeweb::Error("plugin_id_mismatch", "Unexpected test plugin id");

        webview_.bindPluginFactory("plugin.open", id, "counter_args");
        webview_.bind("test.plugin.busy", [this, id]() {
            try {
                webview_.unloadPlugin(id);
            } catch (const nativeweb::Error& error) {
                if (error.code() == "plugin_busy") {
                    std::cout << "checkpoint: plugin-busy-verified" << std::endl;
                    return true;
                }
                throw;
            }
            throw nativeweb::Error("plugin_test_failed", "Unload succeeded with a live object");
        });
        webview_.bind("test.plugin.unload", [this, id]() {
            webview_.unloadPlugin(id);
            std::cout << "checkpoint: plugin-unloaded" << std::endl;
            return true;
        });
        webview_.bind("test.plugin.done", [this]() {
            success_ = true;
            std::cout << "checkpoint: plugin-e2e-complete" << std::endl;
            webview_.destroy();
            return true;
        });
    }

    bool success() const
    {
        return success_;
    }

    const std::string& failure() const
    {
        return failure_;
    }

    void onCreated() override
    {
        browserThreadId_ =
            std::this_thread::get_id();
        browserThreadKnown_.store(true);
    }

    void onLoadStarted(
        const std::string& source) override
    {
        std::cout << "load-start: "
                  << source << std::endl;
    }

    void onLoadFinished(const std::string& source) override
    {
        std::cout << "load-finish: " << source << std::endl;
        const bool pythonPage =
            source == "app://desktop/managed_bridge_test.html" &&
            !managedPython_.empty();
        const bool dotnetPage =
            source == "app://desktop/managed_dotnet_test.html" &&
            !managedDotnetAssembly_.empty();
        if ((!pythonPage && !dotnetPage) || managedReady_.load()) return;
        try {
            if (pythonPage) {
                webview_.bindManagedWorker("example_py", {
                    "example_py.Player.create", "example_py.Player.method.hit",
                    "example_py.Player.get.health", "example_py.Player.set.health",
                    "example_py.Player.dispose"
                }, {managedPython_, managedFixture_, "normal"}, 5000);
            } else {
                webview_.bindManagedWorker("example_cs", {
                    "example_cs.Player.create", "example_cs.Player.method.Hit",
                    "example_cs.Player.get.Health", "example_cs.Player.set.Health",
                    "example_cs.Player.dispose"
                }, {managedDotnetRuntime_, managedDotnetAssembly_}, 5000);
            }
            managedReady_.store(true);
        } catch (const std::exception& error) {
            failure_ = error.what();
            std::cerr << "managed-worker-setup-failed: " << error.what() << std::endl;
            webview_.destroy();
        }
    }

    void onClosed() override
    {
        std::cout
            << "checkpoint: browser-closed"
            << std::endl;
        CefQuitMessageLoop();
    }

private:
    std::string managedPython_;
    std::string managedFixture_;
    std::string managedDotnetRuntime_;
    std::string managedDotnetAssembly_;
    std::atomic<bool> managedReady_{false};

    void beginCppCalls()
    {
        if (cppCallsStarted_)
            return;

        cppSyncResult_ =
            webview_.execute<int>(
                "ui.multiply",
                6,
                7);

        cppAsyncResult_ =
            webview_.execute<int>(
                "ui.multiplyAsync",
                6,
                7);

        cppRejectResult_ =
            webview_.execute<int>(
                "ui.rejectAsync");

        cppCallsStarted_ = true;
    }

    VariantDict cppCallsStatus()
    {
        VariantDict status;
        status["started"] = cppCallsStarted_;
        status["done"] = false;
        status["success"] = false;
        status["message"] = failure_;

        if (!cppCallsStarted_)
            return status;

        if (!cppCallsEvaluated_)
        {
            const std::future_status syncStatus =
                cppSyncResult_.wait_for(
                    std::chrono::milliseconds(0));

            const std::future_status asyncStatus =
                cppAsyncResult_.wait_for(
                    std::chrono::milliseconds(0));

            const std::future_status rejectStatus =
                cppRejectResult_.wait_for(
                    std::chrono::milliseconds(0));

            if (syncStatus != std::future_status::ready ||
                asyncStatus != std::future_status::ready ||
                rejectStatus != std::future_status::ready)
            {
                return status;
            }

            bool syncSuccess = false;
            bool asyncSuccess = false;
            bool rejectSuccess = false;

            try
            {
                syncSuccess =
                    cppSyncResult_.get() == 42;
            }
            catch (const std::exception& error)
            {
                failure_ =
                    std::string(
                        "C++ to sync JS call failed: ") +
                    error.what();
            }

            try
            {
                asyncSuccess =
                    cppAsyncResult_.get() == 42;
            }
            catch (const std::exception& error)
            {
                failure_ =
                    std::string(
                        "C++ to async JS call failed: ") +
                    error.what();
            }

            try
            {
                (void)cppRejectResult_.get();

                if (failure_.empty())
                {
                    failure_ =
                        "Rejected JavaScript Promise "
                        "resolved unexpectedly";
                }
            }
            catch (const nativeweb::Error& error)
            {
                rejectSuccess =
                    error.code() ==
                        "js_promise_rejected" &&
                    std::string(error.what()).find(
                        "expected js rejection") !=
                        std::string::npos;

                if (!rejectSuccess && failure_.empty())
                {
                    failure_ =
                        std::string(
                            "Unexpected JavaScript "
                            "rejection mapping: ") +
                        error.code() +
                        ": " +
                        error.what();
                }
            }
            catch (const std::exception& error)
            {
                if (failure_.empty())
                {
                    failure_ =
                        std::string(
                            "Unexpected C++ exception "
                            "for JS rejection: ") +
                        error.what();
                }
            }

            cppCallsSuccess_ =
                syncSuccess &&
                asyncSuccess &&
                rejectSuccess &&
                failure_.empty();

            cppCallsEvaluated_ = true;
        }

        status["done"] = cppCallsEvaluated_;
        status["success"] = cppCallsSuccess_;
        status["message"] = failure_;
        return status;
    }

    nativeweb::WebView& webview_;
    bool success_;
    bool cppCallsStarted_;
    bool cppCallsEvaluated_;
    bool cppCallsSuccess_;
    bool pendingDestroyStarted_;
    std::future<int> cppSyncResult_;
    std::future<int> cppAsyncResult_;
    std::future<int> cppRejectResult_;
    std::future<int> pendingDestroyResult_;
    std::string failure_;

    std::thread::id browserThreadId_;
    std::atomic<bool> browserThreadKnown_;
    std::atomic<int> activeConcurrentCalls_;
    std::atomic<int> maxConcurrentCalls_;
};

std::string getPluginPath(int argc, char* argv[])
{
    const std::string prefix = "--plugin-path=";
    for (int i = 1; i < argc; ++i) {
        const std::string value = argv[i];
        if (value.compare(0, prefix.size(), prefix) == 0)
            return value.substr(prefix.size());
    }
    return std::string();
}

std::string getUrl(int argc, char* argv[])
{
    const std::string prefix = "--url=";

    for (int i = 1; i < argc; ++i)
    {
        const std::string value = argv[i];

        if (value.compare(0, prefix.size(), prefix) == 0)
            return value.substr(prefix.size());
    }

    return "about:blank";
}

} // namespace

int main(int argc, char* argv[])
{
#if defined(_WIN32)
    CefMainArgs mainArgs(GetModuleHandleW(0));
#else
    CefMainArgs mainArgs(argc, argv);
#endif

    CefRefPtr<nativeweb::detail::CefRendererApp> app(
        new nativeweb::detail::CefRendererApp());

    const int exitCode =
        CefExecuteProcess(mainArgs, app, 0);

    if (exitCode >= 0)
        return exitCode;

    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
#if defined(_WIN32)
    CefString(&settings.log_file) =
        "nativeweb-cef-windows.log";
    settings.log_severity = LOGSEVERITY_INFO;
#else
    settings.log_severity = LOGSEVERITY_WARNING;
#endif

    nativeweb::detail::registerCefBackendFactory();

    nativeweb::WebView webview;
    IntegrationListener listener(webview);
    webview.setListener(&listener);
    try {
        listener.configurePlugin(getPluginPath(argc,argv));
        std::string python,fixture,dotnetRuntime,dotnetAssembly;
        for(int i=1;i<argc;++i){
            const std::string arg=argv[i];
            const std::string pfx="--managed-python=";
            const std::string ffx="--managed-fixture=";
            const std::string drx="--managed-dotnet-runtime=";
            const std::string dax="--managed-dotnet-assembly=";
            if(arg.compare(0,pfx.size(),pfx)==0)python=arg.substr(pfx.size());
            if(arg.compare(0,ffx.size(),ffx)==0)fixture=arg.substr(ffx.size());
            if(arg.compare(0,drx.size(),drx)==0)dotnetRuntime=arg.substr(drx.size());
            if(arg.compare(0,dax.size(),dax)==0)dotnetAssembly=arg.substr(dax.size());
        }
        listener.configureManaged(python,fixture,dotnetRuntime,dotnetAssembly);
    } catch (const std::exception& error) {
        std::cerr << "Plugin E2E setup failed: " << error.what() << std::endl;
        return 5;
    }

    const std::string url = getUrl(argc, argv);
    std::shared_ptr<nativeweb::AppResourceBundle> appAssets;
    if (url.compare(0, 6, "app://") == 0)
    {
        const std::string flag = "--app-assets=";
        std::string directory;
        for (int i=1;i<argc;++i)
        {
            const std::string arg = argv[i];
            if (arg.compare(0,flag.size(),flag)==0)
                directory=arg.substr(flag.size());
        }
        if (directory.empty())
        {
            std::cerr << "app:// test requires --app-assets" << std::endl;
            return 7;
        }
        appAssets.reset(new nativeweb::AppResourceBundle());
        struct AssetEntry { const char* name; const char* url; const char* mime; };
        const AssetEntry assets[] = {
            {"plugin_bridge_test.html", "app://desktop/plugin_bridge_test.html", "text/html"},
            {"managed_bridge_test.html", "app://desktop/managed_bridge_test.html", "text/html"},
            {"managed_dotnet_test.html", "app://desktop/managed_dotnet_test.html", "text/html"},
            {"plugin_untrusted_child.html", "app://desktop/plugin_untrusted_child.html", "text/html"},
            {"app_resource_script.js", "app://desktop/app_resource_script.js", "application/javascript"},
            {"app_resource_style.css", "app://desktop/app_resource_style.css", "text/css"},
            {"app_resource_icon.svg", "app://desktop/app_resource_icon.svg", "image/svg+xml"},
            {"app_camera_private.html", "app://camera/ui/panel.html", "text/html"}
        };
        for (std::size_t i=0; i<sizeof(assets)/sizeof(assets[0]); ++i)
        {
            const std::string path=directory+"/"+assets[i].name;
            std::ifstream input(path.c_str(), std::ios::binary);
            if (!input)
            {
                std::cerr << "Unable to read test bundle asset: "
                          << path << std::endl;
                return 8;
            }
            std::ostringstream contents;
            contents << input.rdbuf();
            appAssets->add(assets[i].url,
                           assets[i].mime,contents.str());
        }
    }

    app->setBrowserReadyCallback(
        [&webview, url, appAssets]() {
            nativeweb::WebViewOptions options;
            options.engine =
                nativeweb::Engine::Cef;
            options.appResources = appAssets;

            webview.create(
                0,
                url,
                options);
        });

    if (!CefInitialize(
            mainArgs,
            settings,
            app,
            0))
    {
        std::cerr << "CefInitialize failed"
                  << std::endl;
        return 2;
    }

    CefRunMessageLoop();

    std::cout
        << "checkpoint: message-loop-returned"
        << std::endl;

    CefShutdown();

    std::cout
        << "checkpoint: cef-shutdown-complete"
        << std::endl;

    if (!listener.success())
    {
        std::cerr
            << "NativeWeb public WebView CEF integration failed";

        if (!listener.failure().empty())
            std::cerr << ": " << listener.failure();

        std::cerr << std::endl;
        return 3;
    }

    std::cout
        << "NativeWeb public WebView CEF integration: PASS"
        << std::endl;

    return 0;
}
