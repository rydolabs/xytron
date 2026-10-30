#ifndef NATIVEWEB_TYPES_HPP_INCLUDED
#define NATIVEWEB_TYPES_HPP_INCLUDED

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nativeweb {
class AppResourceBundle;

// Opaque host-window boundary used by the core API. Host adapters are
// responsible for translating MFC/WinForms/WPF/Qt/GTK/Win32 native handles.
typedef void* NativeWindowHandle;

// Canonical public byte container. Browser backends decide internally whether
// to use ordinary IPC, engine-native binary transport, or shared memory.
typedef std::vector<unsigned char> Binary;
// Convenience spelling for ordinary owned byte arrays; identical to Binary.
typedef Binary ByteArray;

struct NativeObjectHandle
{
    NativeObjectHandle()
        : id(0)
    {
    }

    NativeObjectHandle(
        std::uint64_t valueId,
        const std::string& valueType)
        : id(valueId),
          type(valueType)
    {
    }

    bool valid() const
    {
        return id != 0;
    }

    std::uint64_t id;
    std::string type;
};

enum class Engine
{
    Auto,
    Cef,
    WebView2,
    // Registered only by the corresponding platform backend.
    WkWebView,
    WebKitGtk
};

enum class Capability : std::uint64_t
{
    Binary = 1ull << 0,
    Events = 1ull << 1,
    AsyncJavaScript = 1ull << 2,
    Windowless = 1ull << 3
};

class Capabilities
{
public:
    explicit Capabilities(std::uint64_t mask = 0)
        : mask_(mask)
    {
    }

    bool supports(Capability capability) const
    {
        return (mask_ & static_cast<std::uint64_t>(capability)) != 0;
    }

    std::uint64_t mask() const
    {
        return mask_;
    }

private:
    std::uint64_t mask_;
};

struct WebViewOptions
{
    WebViewOptions()
        : engine(Engine::Auto)
    {
    }

    Engine engine;
    // Explicit byte assets to resolve app:// URLs; no implicit disk access.
    std::shared_ptr<const AppResourceBundle> appResources;
};

} // namespace nativeweb

#endif
