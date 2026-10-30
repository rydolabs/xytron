#include "browser/common/browser_backend.hpp"

#include <string>

namespace {

class TestListener : public nativeweb::detail::BrowserBackendListener
{
public:
    virtual void onBrowserCreated()
    {
    }

    virtual void onLoadStarted(const std::string&)
    {
    }

    virtual void onLoadFinished(const std::string&)
    {
    }

    virtual void onBridgeMessage(const Any&)
    {
    }

    virtual void onBrowserClosed()
    {
    }
};

class TestBackend : public nativeweb::detail::BrowserBackend
{
public:
    TestBackend()
        : listener_(0),
          created_(false)
    {
    }

    virtual const char* engineName() const
    {
        return "test";
    }

    virtual nativeweb::Engine engine() const
    {
        return nativeweb::Engine::Cef;
    }

    virtual nativeweb::Capabilities capabilities() const
    {
        return nativeweb::Capabilities(
            static_cast<std::uint64_t>(
                nativeweb::Capability::Events));
    }

    virtual void setListener(
        nativeweb::detail::BrowserBackendListener* listener)
    {
        listener_ = listener;
    }

    virtual void create(
        const nativeweb::detail::BrowserCreateParams& params)
    {
        created_ = true;
        source_ = params.source;

        if (listener_)
            listener_->onBrowserCreated();
    }

    virtual void destroy()
    {
        created_ = false;

        if (listener_)
            listener_->onBrowserClosed();
    }

    virtual bool isCreated() const
    {
        return created_;
    }

    virtual void load(const std::string& source)
    {
        source_ = source;
    }

    virtual void reload()
    {
    }

    virtual void postBridgeMessage(const Any& message)
    {
        lastMessage_ = message;
    }

private:
    nativeweb::detail::BrowserBackendListener* listener_;
    bool created_;
    std::string source_;
    Any lastMessage_;
};

void compileBrowserBackendContract()
{
    TestListener listener;
    TestBackend backend;

    backend.setListener(&listener);

    nativeweb::detail::BrowserCreateParams params;
    params.parent = 0;
    params.source = "index.html";

    backend.create(params);

    VariantDict envelope;
    envelope["method"] = "math.add";
    backend.postBridgeMessage(Any(envelope));

    backend.load("other.html");
    backend.reload();
    backend.destroy();
}

} // namespace
