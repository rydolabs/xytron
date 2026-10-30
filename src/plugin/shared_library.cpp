#include "plugin/shared_library.hpp"

#include "nativeweb/error.hpp"

#include <sstream>
#include <utility>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace nativeweb {
namespace detail {

namespace {

#if defined(_WIN32)

std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty())
        return std::wstring();

    const int size =
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.c_str(),
            static_cast<int>(value.size()),
            0,
            0);

    if (size <= 0)
        throw Error(
            "shared_library_path",
            "Failed to convert shared library path to UTF-16");

    std::wstring output(
        static_cast<std::size_t>(size),
        L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        &output[0],
        size);

    return output;
}

std::string windowsError(DWORD code)
{
    LPSTR buffer = 0;

    const DWORD length =
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            0,
            code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&buffer),
            0,
            0);

    if (!length || !buffer)
    {
        std::ostringstream stream;
        stream << "Windows error " << code;
        return stream.str();
    }

    std::string message(buffer, length);
    LocalFree(buffer);

    while (
        !message.empty() &&
        (message[message.size() - 1] == '\r' ||
         message[message.size() - 1] == '\n'))
    {
        message.erase(message.size() - 1);
    }

    return message;
}

#endif

} // namespace

SharedLibrary::SharedLibrary()
    : handle_(0)
{
}

SharedLibrary::SharedLibrary(
    const std::string& path)
    : handle_(0)
{
    load(path);
}

SharedLibrary::~SharedLibrary()
{
    unload();
}

SharedLibrary::SharedLibrary(
    SharedLibrary&& other)
    : handle_(other.handle_),
      path_(other.path_)
{
    other.handle_ = 0;
    other.path_.clear();
}

SharedLibrary& SharedLibrary::operator=(
    SharedLibrary&& other)
{
    if (this != &other)
    {
        unload();

        handle_ = other.handle_;
        path_ = other.path_;

        other.handle_ = 0;
        other.path_.clear();
    }

    return *this;
}

void SharedLibrary::load(
    const std::string& path)
{
    if (path.empty())
    {
        throw Error(
            "shared_library_path",
            "Shared library path cannot be empty");
    }

    if (loaded())
    {
        throw Error(
            "shared_library_already_loaded",
            "A shared library is already loaded");
    }

#if defined(_WIN32)
    const std::wstring widePath =
        utf8ToWide(path);

    HMODULE module =
        LoadLibraryExW(
            widePath.c_str(),
            0,
            LOAD_WITH_ALTERED_SEARCH_PATH);

    if (!module)
    {
        throw Error(
            "shared_library_load_failed",
            "Failed to load shared library '" +
                path +
                "': " +
                windowsError(GetLastError()));
    }

    handle_ =
        reinterpret_cast<void*>(module);
#else
    dlerror();

    void* module =
        dlopen(
            path.c_str(),
            RTLD_NOW | RTLD_LOCAL);

    if (!module)
    {
        const char* message =
            dlerror();

        throw Error(
            "shared_library_load_failed",
            "Failed to load shared library '" +
                path +
                "': " +
                (message
                    ? std::string(message)
                    : std::string("unknown loader error")));
    }

    handle_ = module;
#endif

    path_ = path;
}

void SharedLibrary::unload()
{
    if (!handle_)
        return;

#if defined(_WIN32)
    FreeLibrary(
        reinterpret_cast<HMODULE>(
            handle_));
#else
    dlclose(handle_);
#endif

    handle_ = 0;
    path_.clear();
}

bool SharedLibrary::loaded() const
{
    return handle_ != 0;
}

void* SharedLibrary::symbol(
    const std::string& name) const
{
    if (!handle_)
    {
        throw Error(
            "shared_library_not_loaded",
            "Cannot resolve a symbol before loading a shared library");
    }

    if (name.empty())
    {
        throw Error(
            "shared_library_symbol",
            "Shared library symbol name cannot be empty");
    }

#if defined(_WIN32)
    FARPROC address =
        GetProcAddress(
            reinterpret_cast<HMODULE>(
                handle_),
            name.c_str());

    if (!address)
    {
        throw Error(
            "shared_library_symbol_not_found",
            "Symbol '" +
                name +
                "' was not found in '" +
                path_ +
                "': " +
                windowsError(GetLastError()));
    }

    return reinterpret_cast<void*>(address);
#else
    dlerror();

    void* address =
        dlsym(
            handle_,
            name.c_str());

    const char* message =
        dlerror();

    if (message)
    {
        throw Error(
            "shared_library_symbol_not_found",
            "Symbol '" +
                name +
                "' was not found in '" +
                path_ +
                "': " +
                std::string(message));
    }

    return address;
#endif
}

const std::string& SharedLibrary::path() const
{
    return path_;
}

} // namespace detail
} // namespace nativeweb
