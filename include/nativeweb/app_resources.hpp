#ifndef NATIVEWEB_APP_RESOURCES_HPP_INCLUDED
#define NATIVEWEB_APP_RESOURCES_HPP_INCLUDED

#include <map>
#include <string>

namespace nativeweb {

// Immutable after passing to a browser backend. No directory traversal or
// implicit filesystem access: each asset is explicitly included by the host.
struct AppResource
{
    std::string mimeType;
    std::string bytes;
};

class AppResourceBundle
{
public:
    // The first app:// slice accepts canonical ASCII host/path URLs only.
    // Examples: app://desktop/index.html, app://camera/ui/panel.html.
    // No query, fragment, percent-escape, dot segment or user info.
    static bool isCanonicalUrl(const std::string& url);

    void add(const std::string& url, const std::string& mimeType,
             const std::string& bytes);
    bool find(const std::string& url, AppResource* result) const;
    bool contains(const std::string& url) const;
    std::size_t size() const;

private:
    std::map<std::string, AppResource> resources_;
    std::size_t totalBytes_ = 0;
};

} // namespace nativeweb
#endif
