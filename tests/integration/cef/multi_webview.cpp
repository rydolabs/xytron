#include "browser/cef/cef_backend.hpp"
#include "browser/cef/cef_renderer_app.hpp"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "nativeweb/error.hpp"
#include "nativeweb/webview.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <string>

namespace {

class Coordinator;

class ViewListener : public nativeweb::WebViewListener
{
public:
    ViewListener(
        Coordinator& coordinator,
        int index,
        const std::string& id);

    void onClosed() override;

private:
    Coordinator& coordinator_;
    int index_;
    std::string id_;
};

class Coordinator
{
public:
    Coordinator()
        : callsStarted_(false),
          callsEvaluated_(false),
          callsSuccess_(false),
          closeStarted_(false),
          closedCount_(0)
    {
        ready_[0] = ready_[1] = false;
        eventReceived_[0] = eventReceived_[1] = false;
        done_[0] = done_[1] = false;
        views_[0] = views_[1] = 0;
    }

    void attach(
        int index,
        nativeweb::WebView* view,
        const std::string& id)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            views_[index] = view;
            ids_[index] = id;
        }

        view->bind(
            "test.identity",
            [id]() {
                return id;
            });

        view->bind(
            "test.ready",
            [this, index, id](const std::string& value) {
                if (value != id)
                {
                    fail(
                        "JS->C++ WebView identity crossed: " +
                        value + " != " + id);
                    return false;
                }

                bool shouldStart = false;

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ready_[index] = true;

                    if (
                        !callsStarted_ &&
                        ready_[0] &&
                        ready_[1] &&
                        views_[0] &&
                        views_[1])
                    {
                        callsStarted_ = true;
                        shouldStart = true;
                    }
                }

                if (shouldStart)
                    startCrossChecks();

                return true;
            });

        view->bind(
            "test.eventReceived",
            [this, index, id](const std::string& value) {
                if (value != id)
                {
                    fail(
                        "Event crossed WebView boundary: " +
                        value + " != " + id);
                    return false;
                }

                std::lock_guard<std::mutex> lock(mutex_);
                eventReceived_[index] = true;
                return true;
            });

        view->bind(
            "test.status",
            [this, index]() {
                return status(index);
            });

        view->bind(
            "test.done",
            [this, index, id](const std::string& value) {
                if (value != id)
                {
                    fail(
                        "Done identity crossed WebView boundary");
                    return false;
                }

                const VariantDict current = status(index);

                if (!AnyCast<bool>(
                        current.find("done")->second) ||
                    !AnyCast<bool>(
                        current.find("success")->second))
                {
                    fail(
                        "WebView completed before isolation "
                        "checks succeeded");
                    return false;
                }

                bool close = false;

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    done_[index] = true;
                    close = done_[0] && done_[1];
                }

                if (close)
                    closeAll();

                return true;
            });

        view->bind(
            "test.fail",
            [this](const std::string& message) {
                fail(message);
                return false;
            });
    }

    void onClosed()
    {
        bool quit = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++closedCount_;
            quit = closedCount_ == 2;
        }

        if (quit)
            CefQuitMessageLoop();
    }

    bool success()
    {
        evaluateCallsIfReady();

        std::lock_guard<std::mutex> lock(mutex_);

        return failure_.empty() &&
            done_[0] &&
            done_[1] &&
            callsEvaluated_ &&
            callsSuccess_ &&
            eventReceived_[0] &&
            eventReceived_[1];
    }

    std::string failure() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return failure_;
    }

private:
    void startCrossChecks()
    {
        nativeweb::WebView* first = 0;
        nativeweb::WebView* second = 0;
        std::string firstId;
        std::string secondId;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            first = views_[0];
            second = views_[1];
            firstId = ids_[0];
            secondId = ids_[1];
        }

        VariantDict firstEvent;
        firstEvent["id"] = firstId;

        VariantDict secondEvent;
        secondEvent["id"] = secondId;

        first->emit(
            "isolation.event",
            Any(firstEvent));

        second->emit(
            "isolation.event",
            Any(secondEvent));

        std::future<std::string> firstFuture =
            first->execute<std::string>(
                "ui.identity");

        std::future<std::string> secondFuture =
            second->execute<std::string>(
                "ui.identity");

        {
            std::lock_guard<std::mutex> lock(mutex_);
            firstIdentity_ =
                std::move(firstFuture);
            secondIdentity_ =
                std::move(secondFuture);
        }
    }

    void evaluateCallsIfReady()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (
            !callsStarted_ ||
            callsEvaluated_ ||
            !firstIdentity_.valid() ||
            !secondIdentity_.valid())
        {
            return;
        }

        if (
            firstIdentity_.wait_for(
                std::chrono::milliseconds(0)) !=
                std::future_status::ready ||
            secondIdentity_.wait_for(
                std::chrono::milliseconds(0)) !=
                std::future_status::ready)
        {
            return;
        }

        try
        {
            const std::string first =
                firstIdentity_.get();

            const std::string second =
                secondIdentity_.get();

            callsSuccess_ =
                first == ids_[0] &&
                second == ids_[1];

            if (!callsSuccess_ &&
                failure_.empty())
            {
                failure_ =
                    "C++->JS WebView identity crossed: " +
                    first + ", " + second;
            }
        }
        catch (const std::exception& error)
        {
            if (failure_.empty())
            {
                failure_ =
                    std::string(
                        "C++->JS multi-WebView call failed: ") +
                    error.what();
            }
        }

        callsEvaluated_ = true;
    }

    VariantDict status(int index)
    {
        evaluateCallsIfReady();

        std::lock_guard<std::mutex> lock(mutex_);

        VariantDict result;
        result["done"] =
            callsEvaluated_ &&
            eventReceived_[index];

        result["success"] =
            failure_.empty() &&
            callsEvaluated_ &&
            callsSuccess_ &&
            eventReceived_[index];

        result["message"] = failure_;
        return result;
    }

    void fail(const std::string& message)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (failure_.empty())
                failure_ = message;
        }

        closeAll();
    }

    void closeAll()
    {
        nativeweb::WebView* first = 0;
        nativeweb::WebView* second = 0;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (closeStarted_)
                return;

            closeStarted_ = true;
            first = views_[0];
            second = views_[1];
        }

        if (first)
            first->destroy();

        if (second)
            second->destroy();
    }

    mutable std::mutex mutex_;

    nativeweb::WebView* views_[2];
    std::string ids_[2];
    bool ready_[2];
    bool eventReceived_[2];
    bool done_[2];

    bool callsStarted_;
    bool callsEvaluated_;
    bool callsSuccess_;
    bool closeStarted_;
    int closedCount_;

    std::future<std::string> firstIdentity_;
    std::future<std::string> secondIdentity_;
    std::string failure_;

    friend class ViewListener;
};

ViewListener::ViewListener(
    Coordinator& coordinator,
    int index,
    const std::string& id)
    : coordinator_(coordinator),
      index_(index),
      id_(id)
{
}

void ViewListener::onClosed()
{
    coordinator_.onClosed();
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
    settings.log_severity = LOGSEVERITY_WARNING;

    nativeweb::detail::registerCefBackendFactory();

    nativeweb::WebView first;
    nativeweb::WebView second;

    Coordinator coordinator;
    ViewListener firstListener(
        coordinator,
        0,
        "A");
    ViewListener secondListener(
        coordinator,
        1,
        "B");

    first.setListener(&firstListener);
    second.setListener(&secondListener);

    coordinator.attach(0, &first, "A");
    coordinator.attach(1, &second, "B");

    const std::string baseUrl =
        getUrl(argc, argv);

    app->setBrowserReadyCallback(
        [&first, &second, baseUrl]() {
            nativeweb::WebViewOptions options;
            options.engine =
                nativeweb::Engine::Cef;

            first.create(
                0,
                baseUrl + "?id=A",
                options);

            second.create(
                0,
                baseUrl + "?id=B",
                options);
        });

    if (!CefInitialize(
            mainArgs,
            settings,
            app,
            0))
    {
        std::cerr
            << "CefInitialize failed"
            << std::endl;
        return 2;
    }

    CefRunMessageLoop();
    CefShutdown();

    if (!coordinator.success())
    {
        std::cerr
            << "NativeWeb CEF multi-WebView integration failed";

        const std::string failure =
            coordinator.failure();

        if (!failure.empty())
            std::cerr << ": " << failure;

        std::cerr << std::endl;
        return 3;
    }

    std::cout
        << "NativeWeb CEF multi-WebView integration: PASS"
        << std::endl;

    return 0;
}
