#ifndef NATIVEWEB_SHARED_LIBRARY_HPP_INCLUDED
#define NATIVEWEB_SHARED_LIBRARY_HPP_INCLUDED

#include <string>

namespace nativeweb {
namespace detail {

class SharedLibrary
{
public:
    SharedLibrary();
    explicit SharedLibrary(const std::string& path);
    ~SharedLibrary();

    SharedLibrary(SharedLibrary&& other);
    SharedLibrary& operator=(SharedLibrary&& other);

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    void load(const std::string& path);
    void unload();

    bool loaded() const;
    void* symbol(const std::string& name) const;

    const std::string& path() const;

private:
    void* handle_;
    std::string path_;
};

} // namespace detail
} // namespace nativeweb

#endif
