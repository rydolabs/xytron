#include "browser/webview2/webview2_backend.hpp"
#include "nativeweb/webview.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <string>

#include <windows.h>

namespace {

class Coordinator;

class ViewListener : public nativeweb::WebViewListener
{
public:
    explicit ViewListener(Coordinator& coordinator)
        : coordinator_(coordinator)
    {
    }

    void onClosed() override;

private:
    Coordinator& coordinator_;
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
            [this, index, id](
                const std::string& value)
            {
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
            [this, index, id](
                const std::string& value)
            {
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
            [this, index, id](
                const std::string& value)
            {
                if (value != id)
                {
                    fail(
                        "Done identity crossed WebView boundary");
                    return false;
                }

                const VariantDict current =
                    status(index);

                if (
                    !AnyCast<bool>(
                        current.find("done")->second) ||
                    !AnyCast<bool>(
                        current.find("success")->second))
                {
                    fail(
                        "WebView completed before isolation checks succeeded");
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
            PostQuitMessage(0);
    }

    bool success()
    {
        evaluateCallsIfReady();

        std::lock_guard<std::mutex> lock(mutex_);

        return
            failure_.empty() &&
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
};

void ViewListener::onClosed()
{
    coordinator_.onClosed();
}

LRESULT CALLBACK hostWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    return DefWindowProcW(
        window,
        message,
        wParam,
        lParam);
}

HWND createHostWindow(
    const wchar_t* title)
{
    const wchar_t* className =
        L"NativeWeb.WebView2.MultiIntegrationHost";

    WNDCLASSW windowClass;
    ZeroMemory(
        &windowClass,
        sizeof(windowClass));

    windowClass.lpfnWndProc =
        &hostWindowProc;
    windowClass.hInstance =
        GetModuleHandleW(0);
    windowClass.lpszClassName =
        className;

    if (!RegisterClassW(&windowClass))
    {
        if (
            GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS)
        {
            return 0;
        }
    }

    return CreateWindowExW(
        0,
        className,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        480,
        0,
        0,
        GetModuleHandleW(0),
        0);
}

std::string getUrl(
    int argc,
    char* argv[])
{
    const std::string prefix =
        "--url=";

    for (int i = 1; i < argc; ++i)
    {
        const std::string value =
            argv[i];

        if (
            value.compare(
                0,
                prefix.size(),
                prefix) == 0)
        {
            return value.substr(
                prefix.size());
        }
    }

    return "about:blank";
}

} // namespace

int main(
    int argc,
    char* argv[])
{
    HWND firstWindow =
        createHostWindow(
            L"NativeWeb WebView2 A");

    HWND secondWindow =
        createHostWindow(
            L"NativeWeb WebView2 B");

    if (!firstWindow || !secondWindow)
    {
        std::cerr
            << "Failed to create multi-WebView host windows"
            << std::endl;
        return 2;
    }

    if (!nativeweb::detail::isWebView2RuntimeAvailable())
    {
        std::cerr
            << "WebView2 Runtime is not available"
            << std::endl;
        DestroyWindow(firstWindow);
        DestroyWindow(secondWindow);
        return 5;
    }

    nativeweb::detail::
        registerWebView2BackendFactory();

    nativeweb::WebView first;
    nativeweb::WebView second;

    Coordinator coordinator;
    ViewListener firstListener(coordinator);
    ViewListener secondListener(coordinator);

    first.setListener(&firstListener);
    second.setListener(&secondListener);

    coordinator.attach(
        0,
        &first,
        "A");

    coordinator.attach(
        1,
        &second,
        "B");

    const std::string baseUrl =
        getUrl(argc, argv);

    nativeweb::WebViewOptions options;
    options.engine =
        nativeweb::Engine::WebView2;

    try
    {
        first.create(
            reinterpret_cast<
                nativeweb::NativeWindowHandle>(
                    firstWindow),
            baseUrl + "?id=A",
            options);

        second.create(
            reinterpret_cast<
                nativeweb::NativeWindowHandle>(
                    secondWindow),
            baseUrl + "?id=B",
            options);
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "WebView2 multi create failed: "
            << error.what()
            << std::endl;

        DestroyWindow(firstWindow);
        DestroyWindow(secondWindow);
        return 3;
    }

    MSG message;

    while (
        GetMessageW(
            &message,
            0,
            0,
            0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DestroyWindow(firstWindow);
    DestroyWindow(secondWindow);

    if (!coordinator.success())
    {
        std::cerr
            << "NativeWeb WebView2 multi-WebView integration failed";

        const std::string failure =
            coordinator.failure();

        if (!failure.empty())
            std::cerr << ": " << failure;

        std::cerr << std::endl;
        return 4;
    }

    std::cout
        << "NativeWeb WebView2 multi-WebView integration: PASS"
        << std::endl;

    return 0;
}
